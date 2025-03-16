#include "JIT/JITEngine.h"
#include "JIT/JITRuntime.h"
#include "Minidump/MinidumpContext.h"
#include "Disasm/BasicBlockDisassembler.h"
#include "Lift/BasicBlockLifter.h"
#include "Prebuilt/Utils.h"
#include "BitcodeManipulation/BitcodeManipulation.h"

#include "remill/Arch/X86/Runtime/State.h"

#include <iostream>
#include <sstream>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

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
#include <llvm/Transforms/Utils/Cloning.h>

using namespace llvm;

// Define command line flags
DEFINE_string(minidump, "", "Path to the minidump file (REQUIRED)");
DEFINE_uint64(stop_addr, 0, "Address to stop execution at (REQUIRED)");
DEFINE_uint32(max_translations, 50, "Maximum number of translations to perform");
DEFINE_bool(help_all, false, "Show all help options");

namespace Recycle {

// Command line options class to handle program arguments
class Options {
public:
    Options(int argc, char* argv[]) {
        // Flags should already be parsed in main()
        
        // Validate required flags
        if (FLAGS_minidump.empty()) {
            gflags::ShowUsageWithFlagsRestrict(argv[0], "src/recycle.cpp");
            throw std::runtime_error("--minidump is required");
        }
        
        if (FLAGS_stop_addr == 0) {
            gflags::ShowUsageWithFlagsRestrict(argv[0], "src/recycle.cpp");
            throw std::runtime_error("--stop_addr is required");
        }
        
        // Store values in class members
        minidumpPath = FLAGS_minidump;
        stopAddr = FLAGS_stop_addr;
        // print the stop address in hex
        LOG(INFO) << "Stop address: 0x" << std::hex << stopAddr;
        maxTranslations = FLAGS_max_translations;
    }
    
    std::string getMinidumpPath() const { return minidumpPath; }
    uint64_t getStopAddr() const { return stopAddr; }
    size_t getMaxTranslations() const { return maxTranslations; }
    
private:
    std::string minidumpPath;
    uint64_t stopAddr = 0;  // 0x140001862
    size_t maxTranslations = 50;
};

// Memory reader interface to abstract memory access
class MemoryReader {
public:
    virtual ~MemoryReader() = default;
    
    // Read memory from the specified address
    virtual std::vector<uint8_t> ReadMemory(uint64_t address, size_t size) const = 0;
    
    // Get the entry point (instruction pointer)
    virtual uint64_t GetEntryPoint() const = 0;
    
    // Get thread TEB address if available
    virtual uint64_t GetThreadTebAddress() const = 0;
};

// Minidump implementation of the memory reader interface
class MinidumpMemoryReader : public MemoryReader {
public:
    MinidumpMemoryReader(const std::string& minidumpPath) : minidump(minidumpPath) {
        if (!minidump.Initialize()) {
            throw std::runtime_error("Failed to initialize minidump context");
        }
    }
    
    std::vector<uint8_t> ReadMemory(uint64_t address, size_t size) const override {
        return minidump.ReadMemory(address, size);
    }
    
    uint64_t GetEntryPoint() const override {
        return minidump.GetInstructionPointer();
    }
    
    uint64_t GetThreadTebAddress() const override {
        return minidump.GetThreadTebAddress();
    }
    
private:
    MinidumpContext::MinidumpContext minidump;
};

// Initialize Google logging system
void initializeLogging(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;  // Ensure logs go to stderr
    FLAGS_colorlogtostderr = true;  // Enable colored output
    google::SetLogDestination(google::INFO, "");  // Disable log file output
    FLAGS_log_prefix = false;
    // FLAGS_v is set automatically through command line -v flag
    LOG(INFO) << "Starting program execution";
}

// Initialize minidump from a file
MinidumpContext::MinidumpContext initializeMinidump(const std::string& minidumpPath) {
    LOG(INFO) << "Initializing minidump context from file: " << minidumpPath;
    MinidumpContext::MinidumpContext minidump(minidumpPath);
    if (!minidump.Initialize()) {
        LOG(ERROR) << "Failed to initialize minidump context";
        throw std::runtime_error("Failed to initialize minidump context");
    }
    return minidump;
}

// Setup initial environment for disassembly and lifting
void setupEnvironment(std::unique_ptr<llvm::LLVMContext>& llvm_context, 
                     std::vector<uint64_t>& missing_blocks,
                     uint64_t entry_point,
                     uint64_t stop_addr,
                     std::string& entry_point_name) {
    // Set up address to ignore
    LOG(INFO) << "Adding ignored address: 0x" << std::hex << stop_addr;
    Runtime::MissingBlockTracker::AddIgnoredAddress(stop_addr);

    // Initialize missing blocks with entry point
    missing_blocks.push_back(entry_point);
    VLOG(1) << "Starting disassembly from instruction pointer: 0x" 
            << std::hex << entry_point;

    // Create name for entry point function
    std::stringstream ss;
    ss << "sub_" << std::hex << entry_point;
    entry_point_name = ss.str();
}

// Process missing memory and add it to the module
bool processMissingMemory(std::unique_ptr<llvm::Module>& saved_module, 
                          std::vector<std::pair<uint64_t, uint8_t>>& missing_memory,
                          std::vector<std::pair<uint64_t, uint8_t>>& added_memory,
                          const MemoryReader& memory_reader) {
    LOG(INFO) << "Missing memory: " << missing_memory.size();
    // Read page from the memory source
    const auto page_addr = missing_memory[0].first;
    const auto page_size = PREBUILT_MEMORY_CELL_SIZE;
    const auto page = memory_reader.ReadMemory(page_addr, page_size);
    
    if (page.empty()) {
        LOG(ERROR) << "Failed to read memory at address: 0x" << std::hex << page_addr;
        return false;
    }
    
    // Add missing memory to module
    if (!BitcodeManipulation::AddMissingMemory(*saved_module, page_addr, page)) {
        LOG(ERROR) << "Failed to add missing memory handler";
        return false;
    }
    missing_memory.clear();
    return true;
}

// Lift a basic block and add it to the module
bool liftBasicBlock(std::unique_ptr<llvm::Module>& saved_module,
                   std::vector<std::pair<uint64_t, std::string>>& addr_to_func_map,
                   const MemoryReader& memory_reader,
                   llvm::LLVMContext& llvm_context,
                   uint64_t ip,
                   const std::string& entry_point_name) {
    // Use the provided LLVM context instead of creating a local one
    BasicBlockLifter lifter(llvm_context);
    BasicBlockDisassembler disassembler;

    LOG(INFO) << "Lifting block at IP: 0x" << std::hex << ip;

    // Read memory for the block
    auto memory = memory_reader.ReadMemory(ip, 256); // Read enough for a basic block
    if (memory.empty()) {
        LOG(ERROR) << "Failed to read memory at IP: 0x" << std::hex << ip;
        return false;
    }

    // Disassemble the basic block
    auto instructions = disassembler.DisassembleBlock(memory.data(), memory.size(), ip);
    if (instructions.empty()) {
        LOG(ERROR) << "No instructions decoded at IP: 0x" << std::hex << ip;
        return false;
    }
    VLOG(1) << "Successfully disassembled " << instructions.size() << " instructions";

    // Lift the block
    if (!lifter.LiftBlock(instructions, ip)) {
        LOG(ERROR) << "Failed to lift basic block at IP: 0x" << std::hex << ip;
        return false;
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

    // Save or merge the module
    if (saved_module == nullptr) {
        saved_module = llvm::CloneModule(*lifted_module);
    } else {
        BitcodeManipulation::MergeModules(*saved_module, *lifted_module);
        lifted_module = llvm::CloneModule(*saved_module);
    }

    // Apply the transformations
    BitcodeManipulation::RenameFunctions(*saved_module);
    BitcodeManipulation::RemoveSuffixFromFunctions(*saved_module);
    //BitcodeManipulation::InsertFunctionLogging(*saved_module);

    // Add missing block handler with current mappings
    // BitcodeManipulation::AddMissingBlockHandler(*saved_module, addr_to_func_map);
    // check if there is entry function in the module
    //const auto entry_function = saved_module->getFunction("entry");
    //if (!entry_function) {
    //    const auto utils_module = BitcodeManipulation::ReadBitcodeFile("build/Utils.ll", llvm_context);
    //    if (!utils_module)
    //    {
    //        LOG(ERROR) << "Failed to load Utils.ll module";
    //        return false;
    //    }
    //    BitcodeManipulation::MergeModules(*saved_module, *utils_module);
    //    BitcodeManipulation::CreateEntryFunction(
    //        *saved_module, memory_reader.GetEntryPoint(), memory_reader.GetThreadTebAddress(), entry_point_name);
    //}

    return true;
}

} // namespace Recycle

int main(int argc, char* argv[]) {
    // Initialize logging
    Recycle::initializeLogging(argc, argv);

    try {
        // Set up gflags
        std::string usage = std::string("Usage: ") + argv[0] + " --minidump=<path_to_minidump> --stop_addr=<hex_address> [options]";
        gflags::SetUsageMessage(usage);
        gflags::SetVersionString("1.0.0");
        
        // Parse command line flags
        gflags::ParseCommandLineFlags(&argc, &argv, true);
        
        // If help requested, show usage and exit
        if (FLAGS_help_all) {
            gflags::ShowUsageWithFlagsRestrict(argv[0], "src/recycle.cpp");
            return 0;
        }
        
        // Parse command line options
        Recycle::Options options(argc, argv);
        
        // Create memory reader from minidump
        Recycle::MinidumpMemoryReader memory_reader(options.getMinidumpPath());
        uint64_t entry_point = memory_reader.GetEntryPoint();
        
        // Setup environment - create a single LLVM context that will be shared throughout execution
        auto llvm_context = std::make_unique<llvm::LLVMContext>();
        
        std::vector<uint64_t> missing_blocks;
        std::set<uint64_t> processed_blocks = {options.getStopAddr()};
        std::string entry_point_name;
        Recycle::setupEnvironment(llvm_context, missing_blocks, entry_point, options.getStopAddr(), entry_point_name);

        // Data tracking structures
        std::vector<std::pair<uint64_t, std::string>> addr_to_func_map;
        std::vector<std::pair<uint64_t, uint8_t>> missing_memory;
        std::vector<std::pair<uint64_t, uint8_t>> added_memory;
        uint64_t ip = 0;
        size_t iteration_count = 0;

        // lambda to get name to dump module
        auto get_filename_prefix = [&](std::string purpose, size_t iteration_count) {
            std::stringstream ss;
            ss << "lifted-" << std::setw(4) << std::setfill('0') << iteration_count << "_" << purpose << ".ll";
            return ss.str();
        };

        const auto utils_module = BitcodeManipulation::ReadBitcodeFile(
            "build/Utils_manual.ll", *llvm_context);
        if (!utils_module)
        {
            LOG(ERROR) << "Failed to load Utils_manual.ll module";
            return 1;
        }

        const auto opt_exclustion = std::vector<std::string>{"main", "Stack", "GlobalRcx"};

        // merge with utils module
        std::unique_ptr<llvm::Module> merged_module = llvm::CloneModule(*utils_module);
        BitcodeManipulation::SetGlobalVariableUint64(*merged_module, "StartPC", entry_point);
        BitcodeManipulation::SetGlobalVariableUint64(*merged_module, "GSBase", memory_reader.GetThreadTebAddress());
        BitcodeManipulation::SetGlobalVariableUint64(*merged_module, "GlobalRcx", 0);
        // Main processing loop
        while ((missing_blocks.size() > 0 || missing_memory.size() > 0) && 
               iteration_count < options.getMaxTranslations()) {

            // merge all lifted modules
            std::stringstream ss;
            ss << "sub_" << std::hex << ip;
            const auto lifted_sub_name = ss.str();

            VLOG(1) << "Lifted modules merged";
            if (missing_memory.size() > 0) {
                // TODO:
                throw std::runtime_error("Not implemented");
                //if (!Recycle::processMissingMemory(saved_module, missing_memory, added_memory, memory_reader)) {
                //    return 1;
                //}
            } else {
                // Process a missing block
                ip = missing_blocks.back();
                missing_blocks.pop_back();
                
                // Pass the shared LLVM context to the liftBasicBlock function
                std::unique_ptr<llvm::Module> lifted_module;
                if (!Recycle::liftBasicBlock(lifted_module, addr_to_func_map, memory_reader, *llvm_context, ip, entry_point_name)) {
                    return 1;
                }

                BitcodeManipulation::DumpModule(*lifted_module,
                                                get_filename_prefix("_0_lifted", iteration_count));

                BitcodeManipulation::MergeModules(*merged_module, *lifted_module);
                BitcodeManipulation::DumpModule(*merged_module,
                                                get_filename_prefix("_1_lifted", iteration_count));
                
            }

            // Update main_next function to call the lifted function
            if (iteration_count == 0) {
                BitcodeManipulation::ReplaceFunction(*merged_module, "main_next", entry_point_name);
            }
            //BitcodeManipulation::AddMissingBlockHandler(*merged_module, addr_to_func_map);

            LOG(INFO) << "Optimized module for the first time";
            BitcodeManipulation::RemoveOptNoneAttribute(*merged_module, opt_exclustion);
            BitcodeManipulation::MakeSymbolsInternal(*merged_module, opt_exclustion);
            BitcodeManipulation::MakeFunctionsInline(*merged_module, opt_exclustion);
            //BitcodeManipulation::ReplaceFunction(*merged_module, "__rt_missing_block", "__remill_missing_block");
            BitcodeManipulation::DumpModule(*merged_module,
                                            get_filename_prefix("_2_pre_opt", iteration_count));
            BitcodeManipulation::ReplaceMissingBlockCalls(*merged_module, "__remill_missing_block");

            BitcodeManipulation::OptimizeModule(*merged_module, 3); // Calling optimize module twice is intentional
            //BitcodeManipulation::OptimizeModule(*merged_module, 3); // Calling optimize module twice is intentional
            BitcodeManipulation::DumpModule(*merged_module,
                                            get_filename_prefix("_4_opt", iteration_count));
            BitcodeManipulation::DumpModule(*merged_module,
                                            get_filename_prefix("_rt", iteration_count));

            // replace write memory with GEP
            // BitcodeManipulation::ReplaceStackMemoryWrites(*merged_module);
            BitcodeManipulation::DumpModule(*merged_module,
                                            get_filename_prefix("_5_memory_writes", iteration_count));

            auto new_missing_blocks = BitcodeManipulation::ExtractMissingBlocks(*merged_module, "__remill_missing_block");

            for (const auto &block : new_missing_blocks) {
                if (processed_blocks.count(block) == 0) {
                    missing_blocks.push_back(block);
                    processed_blocks.insert(block);
                }
            }
            BitcodeManipulation::PrintMissingBlocks(missing_blocks);

            if (iteration_count >= 3) {
                break;
            }
            iteration_count++;
        }

        return 0;
    }
    catch (const std::exception& e) {
        LOG(ERROR) << "Exception: " << e.what();
        return 1;
    }
} 