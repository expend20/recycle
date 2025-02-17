#include "recycle.h"
#include "JITEngine.h"
#include "JITRuntime.h"
#include "LoggingPass.h"
#include "PassManager.h"
#include "MiscUtils.h"
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

    // Create a new module
    auto accumulated_module = std::make_unique<Module>("accumulated", module->getContext());
    //Continue: create interface to extract missing blocks, merge the lifted blocks and continue until there is no missing blocks
    MiscUtils::MergeModules(*accumulated_module, *module);
    MiscUtils::DumpModule(*accumulated_module, "accumulated.ll");

    // Apply the logging pass using our wrapper
    PassManagerWrapper pass_manager;
    pass_manager.ApplyRenamePass(module.get());
    pass_manager.ApplyLoggingPass(module.get());

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
    const size_t StackSize = 0x100000;
    state.gpr.rsp.qword = StackSize;

    auto mem = std::make_unique<char[]>(StackSize);
    std::memset(mem.get(), 0, StackSize);

    // Execute the lifted code
    if (!jit.ExecuteFunction("sub_14000185d", &state, ip, mem.get())) {
        std::cerr << "Failed to execute lifted code\n";
        return 1;
    }
    
    llvm::outs() << "Successfully executed lifted code\n";
    llvm::outs() << "State: rip: " << llvm::format_hex(state.gpr.rip.qword, 16) << "\n";

    return 0;
} 