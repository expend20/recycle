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

    // Initialize JIT engine
    JITEngine jit;
    if (!jit.Initialize()) {
        std::cerr << "Failed to initialize JIT engine\n";
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

    // Store functions that need mapping
    std::vector<std::pair<std::string, void*>> mappings;
    for (auto &F : *module) {
        if (F.isDeclaration()) {
            std::string name = F.getName().str();
            void* addr = nullptr;
            // Map runtime functions
            if (name == "ReadMemory64") {
                addr = reinterpret_cast<void*>(Runtime::ReadMemory64);
            } else if (name == "WriteMemory64") {
                addr = reinterpret_cast<void*>(Runtime::WriteMemory64);
            } else if (name == "GetRegister") {
                addr = reinterpret_cast<void*>(Runtime::GetRegister);
            } else if (name == "SetRegister") {
                addr = reinterpret_cast<void*>(Runtime::SetRegister);
            } else if (name == "GetFlag") {
                addr = reinterpret_cast<void*>(Runtime::GetFlag);
            } else if (name == "SetFlag") {
                addr = reinterpret_cast<void*>(Runtime::SetFlag);
            } else if (name == "__remill_missing_block") {
                addr = reinterpret_cast<void*>(Runtime::__remill_missing_block);
            } else if (name == "__remill_write_memory_64") {
                addr = reinterpret_cast<void*>(Runtime::__remill_write_memory_64);
            }
            if (addr) {
                mappings.push_back({name, addr});
            }
        }
    }

    // First set up the JIT engine with the module
    if (!jit.InitializeWithModule(std::move(module))) {
        std::cerr << "Failed to initialize JIT engine with module\n";
        return 1;
    }

    // Apply function mappings before execution
    for (const auto& [name, addr] : mappings) {
        jit.AddExternalMappingByName(name, addr);
    }

    // Create state and memory for execution
    Runtime::State state = {};  // Zero-initialized state
    char mem[4096] = {0};      // Simplified memory structure

    // Set up initial state
    state.x86_state.gpr.rip = ip;
    state.x86_state.gpr.rsp = 0x1000;  // Some reasonable stack pointer

    // Execute the main lifted function
    std::stringstream ss;
    ss << "sub_" << std::hex << ip;
    std::string func_name = ss.str();
    
    if (!jit.ExecuteFunction(func_name, &state, ip, mem)) {
        std::cerr << "Failed to execute lifted function\n";
        return 1;
    }

    llvm::outs() << "Successfully executed lifted code\n";
    return 0;
} 