#include "JIT/JITEngine.h"
#include "JIT/JITRuntime.h"
#include "Minidump/MinidumpContext.h"
#include "Disasm/BasicBlockDisassembler.h"
#include "Lift/BasicBlockLifter.h"

#include "BitcodeManipulation/MiscUtils.h"
#include "BitcodeManipulation/InsertLogging.h"
#include "BitcodeManipulation/RemoveSuffix.h"
#include "BitcodeManipulation/Rename.h"

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

// Define CMAKE_BINARY_DIR for development
#ifndef CMAKE_BINARY_DIR
#define CMAKE_BINARY_DIR "build"
#endif

int main(int argc, char* argv[]) {
    // Initialize Google logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;  // Ensure logs go to stderr
    FLAGS_colorlogtostderr = true;  // Enable colored output
    google::SetLogDestination(google::INFO, "");  // Disable log file output
    FLAGS_log_prefix = false;
    FLAGS_v = 0;  // Disable verbose logging
    LOG(INFO) << "Starting program execution";

    if (argc != 2) {
        LOG(ERROR) << "Invalid number of arguments. Usage: " << argv[0] << " <path_to_minidump>";
        return 1;
    }

    LOG(INFO) << "Initializing minidump context from file: " << argv[1];
    // Initialize minidump context
    MinidumpContext::MinidumpContext context(argv[1]);
    if (!context.Initialize()) {
        LOG(ERROR) << "Failed to initialize minidump context";
        return 1;
    }

    // Set up disassembler and lifter
    VLOG(1) << "Setting up disassembler";
    BasicBlockDisassembler disassembler;

    // Create LLVM context that will be shared
    VLOG(1) << "Creating LLVM context";
    std::unique_ptr<llvm::LLVMContext> llvm_context = std::make_unique<llvm::LLVMContext>();

    // Add the address of the function to ignore, the end of the trace
    LOG(INFO) << "Adding ignored address: 0x140001862";
    Runtime::MissingBlockTracker::AddIgnoredAddress(0x140001862);

    std::vector<uint64_t> missing_blocks;
    const auto entry_point = context.GetInstructionPointer();
    missing_blocks.push_back(entry_point);
    VLOG(1) << "Starting disassembly from instruction pointer: 0x" 
              << std::hex << entry_point;

    std::stringstream ss;
    ss << "sub_" << std::hex << entry_point;
    const auto entry_point_name = ss.str();

    // clone the module
    std::unique_ptr<llvm::Module> saved_module;
    std::vector<uint64_t> lifted_ips;
    // Track function names for each lifted block
    std::vector<std::pair<uint64_t, std::string>> addr_to_func_map;

    while (missing_blocks.size() > 0) {
        LOG(INFO) << std::endl;

        BasicBlockLifter lifter(*llvm_context);

        // pop first block from missing_blocks
        uint64_t ip = missing_blocks.back();
        missing_blocks.pop_back();
        LOG(INFO) << "Lifting block #" << lifted_ips.size() << " at IP: 0x" << std::hex << ip;

        auto memory = context.ReadMemory(ip, 256); // Read enough for a basic block
        if (memory.empty())
        {
            LOG(ERROR) << "Failed to read memory at IP: 0x" << std::hex << ip;
            return 1;
        }

        // Disassemble the basic block
        auto instructions = disassembler.DisassembleBlock(memory.data(), memory.size(), ip);
        if (instructions.empty())
        {
            LOG(ERROR) << "No instructions decoded at IP: 0x" << std::hex << ip;
            return 1;
        }
        VLOG(1) << "Successfully disassembled " << instructions.size() << " instructions";

        // Initialize lifter and lift the block
        if (!lifter.LiftBlock(instructions, ip))
        {
            LOG(ERROR) << "Failed to lift basic block at IP: 0x" << std::hex << ip;
            return 1;
        }
        VLOG(1) << "Successfully lifted basic block at IP: 0x" << std::hex << ip;

        // Get the module from lifter
        auto lifted_module = lifter.TakeModule();

        // Create function name for this block
        std::stringstream block_ss;
        block_ss << "sub_" << std::hex << ip;
        const auto block_func_name = block_ss.str();
        
        // Add mapping for this block
        addr_to_func_map.emplace_back(ip, block_func_name);

        // save the module
        if (saved_module == nullptr) {
            saved_module = BitcodeManipulation::CloneModule(*lifted_module);
        }
        else {
            BitcodeManipulation::MergeModules(*saved_module, *lifted_module);
            lifted_module = BitcodeManipulation::CloneModule(*saved_module);
        }

        // Apply the transformations
        BitcodeManipulation::RenameFunctions(*saved_module);
        BitcodeManipulation::RemoveSuffixFromFunctions(*saved_module);
        BitcodeManipulation::InsertFunctionLogging(*saved_module);

        // Add missing block handler with current mappings
        BitcodeManipulation::AddMissingBlockHandler(*saved_module, addr_to_func_map);

        BitcodeManipulation::CreateEntryWithState(*saved_module, entry_point, context.GetThreadTebAddress(), entry_point_name);

        // log .ll file
        std::stringstream ss;
        ss << "lifted-" << std::setfill('0') << std::setw(3) << lifted_ips.size() << "-" << std::hex << ip << ".ll";
        const auto filename = ss.str();
        BitcodeManipulation::DumpModule(*saved_module, filename);

        // Initialize JIT engine with the updated module that includes the missing block handler
        auto jit_module = BitcodeManipulation::CloneModule(*saved_module);
        JITEngine jit;
        if (!jit.Initialize(std::move(jit_module)))
        {
            LOG(ERROR) << "Failed to initialize JIT engine";
            return 1;
        }
        VLOG(1) << "Successfully initialized JIT engine";

        // Execute the lifted code
        if (!jit.ExecuteFunction("entry"))
        {
            LOG(ERROR) << "Failed to execute lifted code at IP: 0x" << std::hex << entry_point;
            return 1;
        }
        VLOG(1) << "Successfully executed lifted code at IP: 0x" << std::hex << entry_point;


        const auto &new_missing_blocks = Runtime::MissingBlockTracker::GetMissingBlocks();
        // Print all missing blocks encountered during execution
        LOG(INFO) << "New missing blocks encountered (" << new_missing_blocks.size() << "):\n";
        for (const auto &pc : new_missing_blocks)
        {
            LOG(INFO) << "  " << std::hex << pc;
            missing_blocks.push_back(pc);
        }
        // merge new_missing_blocks with missing_blocks
        // add lifted ip to ignored blocks
        Runtime::MissingBlockTracker::ClearMissingBlocks();

        lifted_ips.push_back(ip);

        LOG(INFO) << "Total lifted ips: " << lifted_ips.size();
        LOG(INFO) << "Total missing blocks atm: " << missing_blocks.size();


        if (lifted_ips.size() == 5) {

            break;
        }

    }

    LOG(INFO) << "Program completed successfully";
    return 0;
} 