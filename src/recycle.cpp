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

    // Initialize JIT engine
    JITEngine jit;
    if (!jit.Initialize()) {
        std::cerr << "Failed to initialize JIT engine\n";
        return 1;
    }

    // Initialize with module
    if (!jit.InitializeWithModule(std::move(module))) {
        std::cerr << "Failed to initialize JIT engine with module\n";
        return 1;
    }

    // create state and memory
    llvm::outs() << "Creating state and memory\n";
    X86State state = {};
    state.gpr.rcx.qword = 0x123;
    state.gpr.rip.qword = ip;

    auto mem = std::make_unique<char[]>(1024*1024);
    std::memset(mem.get(), 0, 1024*1024);

    // Execute the lifted code
    if (!jit.ExecuteFunction("sub_14000185d", &state, ip, mem.get())) {
        std::cerr << "Failed to execute lifted code\n";
        return 1;
    }
    
    llvm::outs() << "Successfully executed lifted code\n";
    llvm::outs() << "State: rip: " << llvm::format_hex(state.gpr.rip.qword, 16) << "\n";

    return 0;
} 