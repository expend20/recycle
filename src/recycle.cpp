#include "recycle.h"
#include "JITEngine.h"
#include "Runtime.h"
#include "LoggingPass.h"
#include <iostream>
#include <sstream>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <glog/logging.h>

// jit
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Host.h>
#include "remill/Arch/X86/Runtime/State.h"

using namespace llvm;

int main(int argc, char* argv[]) {
    // Initialize Google logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;  // Ensure logs go to stderr

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_minidump>\n";
        return 1;
    }

    // Initialize minidump context
    MinidumpContext context(argv[1]);
    if (!context.Initialize()) {
        std::cerr << "Failed to initialize minidump context\n";
        return 1;
    }

    // Get instruction pointer and read memory
    uint64_t ip = context.GetInstructionPointer();
    llvm::outs() << "Starting disassembly from instruction pointer: 0x" 
                 << llvm::format_hex(ip, 16) << "\n";

    auto memory = context.ReadMemoryAtIP(256); // Read enough for a basic block
    if (memory.empty()) {
        std::cerr << "Failed to read memory at IP\n";
        return 1;
    }

    llvm::outs() << "Successfully read " << memory.size() 
                 << " bytes of memory for disassembly\n";

    // Disassemble the basic block
    BasicBlockDisassembler disassembler;
    auto instructions = disassembler.DisassembleBlock(memory.data(), memory.size(), ip);

    if (instructions.empty()) {
        std::cerr << "No instructions decoded\n";
        return 1;
    }

    // Initialize lifter and lift the block
    BasicBlockLifter lifter;
    if (!lifter.LiftBlock(instructions, ip)) {
        std::cerr << "Failed to lift basic block\n";
        return 1;
    }

    // Get the module from lifter
    auto module = lifter.TakeModule();

    // Set up the pass manager infrastructure
    llvm::PassBuilder PB;
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    // Register all the basic analyses with the managers
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Create and run the logging pass
    llvm::ModulePassManager MPM;
    MPM.addPass(FunctionLoggingPass());
    MPM.run(*module, MAM);

    // Log all functions in module
    llvm::outs() << "Functions in module before mapping:\n";
    for (auto &F : *module) {
        llvm::outs() << "  " << F.getName() << " (is declaration: " << F.isDeclaration() << ")\n";
    }

    // Initialize LLVM's target infrastructure
    llvm::outs() << "Initializing target infrastructure\n";
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Set the data layout before creating the engine
    llvm::outs() << "Setting data layout\n";
    std::unique_ptr<llvm::TargetMachine> TM(
        llvm::EngineBuilder().selectTarget(
            llvm::Triple(llvm::sys::getProcessTriple()),  // Use host triple
            "",
            llvm::sys::getHostCPUName(),
            llvm::SmallVector<std::string, 0>())
    );
    if (!TM) {
        LOG(ERROR) << "Failed to select target";
        return false;
    }
    module->setDataLayout(TM->createDataLayout());
    module->setTargetTriple(llvm::sys::getProcessTriple());

    // Print the lifted LLVM IR
    std::error_code EC;
    llvm::raw_fd_ostream file("lifted.ll", EC);
    if (EC) {
        std::cerr << "Could not open file: " << EC.message() << "\n";
        return 1;
    }
    module->print(file, nullptr);
    file.close();
    llvm::outs() << "LLVM IR written to lifted.ll\n";

    // Create execution engine
    llvm::outs() << "Creating execution engine\n";
    std::string ErrStr;
    auto* modulePtr = module.get();  // Keep a copy of the raw pointer
    std::unique_ptr<llvm::ExecutionEngine> EE(
        llvm::EngineBuilder(std::move(module))
        .setErrorStr(&ErrStr)
        .setEngineKind(llvm::EngineKind::JIT)
        .setMCPU(llvm::sys::getHostCPUName())
        .setOptLevel(llvm::CodeGenOpt::None)
        .create(TM.release())  // Release ownership of TM to ExecutionEngine
    );

    if (!EE) {
        std::cerr << "Failed to create execution engine: " << ErrStr << std::endl;
        return 1;
    }

    // Map external functions
    struct ExternalFunc {
        const char* name;
        void* ptr;
    };

    ExternalFunc externalFuncs[] = {
        {"__remill_log_function", reinterpret_cast<void*>(Runtime::__remill_log_function)},
        {"__remill_missing_block", reinterpret_cast<void*>(Runtime::__remill_missing_block)},
        {"__remill_write_memory_64", reinterpret_cast<void*>(Runtime::__remill_write_memory_64)},
        {"sub_1400016d0", reinterpret_cast<void*>(Runtime::__sub_1400016d0)}
    };

    for (const auto& func : externalFuncs) {
        llvm::outs() << "Mapping " << func.name << "\n";
        Function* llvmFunc = modulePtr->getFunction(func.name);
        if (!llvmFunc) {
            std::cerr << "Failed to find " << func.name << "\n";
            return 1;
        }
        llvm::outs() << "Function pointer: " << llvm::format_hex(reinterpret_cast<uintptr_t>(func.ptr), 16) << "\n";
        EE->addGlobalMapping(llvmFunc, func.ptr);
    }
    
    // Force symbol resolution
    EE->finalizeObject();
    
    // Verify all external functions are resolved
    for (auto &F : modulePtr->functions()) {
        if (F.isDeclaration()) {
            void *Addr = (void*)EE->getPointerToFunction(&F);
            llvm::outs() << "Function " << F.getName().data() 
                        << " resolved to address: " << llvm::format_hex(reinterpret_cast<uintptr_t>(Addr), 16) << "\n";
        }
    }

    // find and call sub_14000185d
    // define ptr @sub_14000185d(ptr noalias %state, i64 %program_counter, ptr noalias %memory);
    llvm::outs() << "Finding sub_14000185d\n";
    typedef void* (*Sub14000185dFnPtr)(void*, uint64_t, void*);
    Sub14000185dFnPtr Sub14000185dFn = reinterpret_cast<Sub14000185dFnPtr>(EE->getFunctionAddress("sub_14000185d"));
    if (!Sub14000185dFn) {
        std::cerr << "Failed to find sub_14000185d\n";
        return 1;
    }

    // create state and memory
    llvm::outs() << "Creating state and memory\n";
    X86State state = {};
    state.gpr.rcx.qword = 0x123;
    state.gpr.rip.qword = ip;

    auto mem = std::make_unique<char[]>(1024*1024);
    std::memset(mem.get(), 0, 1024*1024);

    // call sub_14000185d
    llvm::outs() << "Calling sub_14000185d\n";
    //__asm__("int3");
    Sub14000185dFn(&state, ip, mem.get());
    
    llvm::outs() << "Successfully executed lifted code\n";
    llvm::outs() << "State: rip: " << llvm::format_hex(state.gpr.rip.qword, 16) << "\n";
    //__asm__("int3");
    //llvm::outs() << "Successfully executed lifted code\n";

    return 0;
} 