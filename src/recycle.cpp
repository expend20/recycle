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

    // Set up disassembler and lifter
    BasicBlockDisassembler disassembler;

    // Create LLVM context that will be shared
    std::unique_ptr<llvm::LLVMContext> llvm_context = std::make_unique<llvm::LLVMContext>();

    // Add the address of the function to ignore, the end of the trace
    Runtime::MissingBlockTracker::AddIgnoredAddress(0x140001862);

    std::vector<uint64_t> missing_blocks;
    missing_blocks.push_back(context.GetInstructionPointer());
    llvm::outs() << "Starting disassembly from instruction pointer: 0x"
                     << llvm::format_hex(missing_blocks[0], 16) << "\n";

    std::stringstream ss;
    ss << "sub_" << std::hex << missing_blocks[0];
    const auto start_name = ss.str();

    // clone the module
    std::unique_ptr<llvm::Module> saved_module;
    size_t count = 0;

    while (missing_blocks.size() > 0) {
        count++;

        BasicBlockLifter lifter(*llvm_context);

        // pop first block from missing_blocks
        uint64_t ip = missing_blocks.back();
        missing_blocks.pop_back();
        llvm::outs() << "Lifting block at IP: 0x" << llvm::format_hex(ip, 16) << "\n";

        auto memory = context.ReadMemory(ip, 256); // Read enough for a basic block
        if (memory.empty())
        {
            std::cerr << "Failed to read memory at IP\n";
            return 1;
        }

        llvm::outs() << "Successfully read " << memory.size()
                     << " bytes of memory for disassembly\n";

        // Disassemble the basic block
        auto instructions = disassembler.DisassembleBlock(memory.data(), memory.size(), ip);
        if (instructions.empty())
        {
            std::cerr << "No instructions decoded\n";
            return 1;
        }

        // Initialize lifter and lift the block
        if (!lifter.LiftBlock(instructions, ip))
        {
            std::cerr << "Failed to lift basic block\n";
            return 1;
        }

        // Get the module from lifter
        auto lifted_module = lifter.TakeModule();

        llvm::outs() << "Merging modules\n";
        // save the module
        if (saved_module == nullptr) {
            saved_module = MiscUtils::CloneModule(*lifted_module);
        }
        else {
            MiscUtils::MergeModules(*saved_module, *lifted_module);
            lifted_module = MiscUtils::CloneModule(*saved_module);
        }

        // Apply the logging pass using our wrapper
        llvm::outs() << "Applying logging pass\n";
        PassManagerWrapper pass_manager;
        pass_manager.ApplyRenamePass(saved_module.get());
        llvm::outs() << "Applying remove suffix pass\n";
        pass_manager.ApplyRemoveSuffixPass(saved_module.get());
        llvm::outs() << "Applying logging pass\n";
        pass_manager.ApplyLoggingPass(saved_module.get());

        // log .ll file
        std::stringstream ss;
        ss << "lifted-" << count << "-" << std::hex << ip << ".ll"; 
        const auto filename = ss.str();
        MiscUtils::DumpModule(*saved_module, filename);

        // Initialize JIT engine
        auto jit_module = MiscUtils::CloneModule(*saved_module);
        llvm::outs() << "Initializing JIT engine\n";
        JITEngine jit;
        if (!jit.Initialize(std::move(jit_module)))
        {
            std::cerr << "Failed to initialize JIT engine\n";
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
        if (!jit.ExecuteFunction(start_name.c_str(), &state, ip, mem.get()))
        {
            std::cerr << "Failed to execute lifted code\n";
            return 1;
        }

        llvm::outs() << "Successfully executed lifted code\n";
        llvm::outs() << "State: rip: " << llvm::format_hex(state.gpr.rip.qword, 16) << "\n";

        // Print all missing blocks encountered during execution
        llvm::outs() << "Missing blocks encountered (" << missing_blocks.size() << "):\n";
        for (const auto &pc : missing_blocks)
        {
            llvm::outs() << "  0x" << llvm::format_hex(pc, 16) << "\n";
        }

        const auto &new_missing_blocks = Runtime::MissingBlockTracker::GetMissingBlocks();
        // merge new_missing_blocks with missing_blocks
        missing_blocks.insert(missing_blocks.end(), new_missing_blocks.begin(), new_missing_blocks.end());
        // add lifted ip to ignored blocks
        Runtime::MissingBlockTracker::ClearIgnoredAddresses();
        Runtime::MissingBlockTracker::AddIgnoredAddress(ip);

        llvm::outs() << "Total missing blocks: " << missing_blocks.size() << "\n";

    }

    return 0;
} 