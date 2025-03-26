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

typedef void(*RuntimeCallbackFn)(void* state, uint64_t* pc, void** memory);
typedef bool(*BruteforceCallbackFn)(uint64_t result); // if returns true, should stop bruteforce

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

// lambda to get name to dump module
std::string getFilenamePrefix(std::string purpose, size_t iteration_count) {
    std::stringstream ss;
    ss << "lifted-" << std::setw(4) << std::setfill('0') << iteration_count << "_" << purpose << ".ll";
    return ss.str();
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
                          std::vector<std::pair<uint64_t, uint8_t>>& added_memory,
                          const MemoryReader& memory_reader) {
    LOG(INFO) << "Processing " << added_memory.size() << " memory items";
    
    // Process each memory item
    for (const auto& mem_item : added_memory) {
        const auto page_addr = mem_item.first;
        const auto page_size = 0x1000; // todo: refactoring artifact
        LOG(INFO) << "Reading memory at address: 0x" << std::hex << page_addr << " with size: " << page_size;
        
        // Read page from the memory source
        const auto page = memory_reader.ReadMemory(page_addr, page_size);
        
        if (page.empty()) {
            LOG(ERROR) << "Failed to read memory at address: 0x" << std::hex << page_addr;
            return false;
        }
        
        // Add missing memory to module
        if (!BitcodeManipulation::AddMissingMemory(*saved_module, page_addr, page)) {
            LOG(ERROR) << "Failed to add missing memory handler for address: 0x" << std::hex << page_addr;
            return false;
        }
    }
    
    return true;
}

// First function to handle just the lifting process
bool liftBasicBlock(std::unique_ptr<llvm::Module>& lifted_module,
                       const MemoryReader& memory_reader,
                       llvm::LLVMContext& llvm_context,
                       uint64_t ip) {
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
    lifted_module = lifter.TakeModule();

    return true;
}

// Second function to handle module manipulation and missing block handling
bool prepareBlockForRun(std::unique_ptr<llvm::Module>& output_module,
                       std::vector<std::pair<uint64_t, std::string>>& addr_to_func_map,
                       const MemoryReader& memory_reader,
                       uint64_t ip,
                       uint64_t entry_point,
                       const std::string& entry_point_name) {
    // Create function name for this block
    std::stringstream block_ss;
    block_ss << "sub_" << std::hex << ip;
    const auto block_func_name = block_ss.str();

    BitcodeManipulation::InsertFunctionLogging(*output_module);

    // Apply the transformations
    BitcodeManipulation::RenameFunctions(*output_module);
    BitcodeManipulation::RemoveSuffixFromFunctions(*output_module);

    // Add mapping for this block
    // check if the block_func_name already exists in addr_to_func_map
    if (std::find_if(addr_to_func_map.begin(), addr_to_func_map.end(), 
                     [&](const auto& pair) { return pair.second == block_func_name; }) != addr_to_func_map.end()) {
        LOG(INFO) << "Block function name already exists: " << block_func_name;
    } else {
        addr_to_func_map.emplace_back(ip, block_func_name);
    }

    // Add missing block handler with current mappings
    BitcodeManipulation::AddMissingBlockHandler(*output_module, addr_to_func_map);

    // Check if there is entry function in the module
    const auto entry_function = output_module->getFunction("main");
    if (!entry_function) {
        const auto utils_module = BitcodeManipulation::ReadBitcodeFile("build/Utils.ll", output_module->getContext());
        if (!utils_module) {
            LOG(ERROR) << "Failed to load Utils.ll module";
            return false;
        }
        BitcodeManipulation::MergeModules(*output_module, *utils_module);
        BitcodeManipulation::ReplaceFunction(*output_module, "main_next", entry_point_name);
        BitcodeManipulation::SetGlobalVariableUint64(*output_module, "StartPC", entry_point);
        BitcodeManipulation::SetGlobalVariableUint64(*output_module, "GSBase", memory_reader.GetThreadTebAddress());
    }

    return true;
}

// Run JIT compilation and execution
bool executeJITCode(std::unique_ptr<llvm::Module>& saved_module,
                   uint64_t ip,
                   uint64_t entry_point,
                   const std::string& filename_prefix,
                   std::vector<std::pair<uint64_t, uint8_t>>& missing_memory,
                   std::vector<std::pair<uint64_t, uint8_t>>& added_memory,
                   std::vector<uint64_t>& missing_blocks) {

    // Dump module to file for debugging
    std::stringstream ss;
    ss << filename_prefix << "-" << std::hex << ip << ".ll";
    const auto filename = ss.str();
    BitcodeManipulation::DumpModule(*saved_module, filename);

    // Initialize JIT engine with the updated module
    auto jit_module = llvm::CloneModule(*saved_module);
    JITEngine jit;
    if (!jit.Initialize(std::move(jit_module))) {
        LOG(ERROR) << "Failed to initialize JIT engine";
        return false;
    }

    // Execute the lifted code
    LOG(INFO) << "Executing lifted code at IP: 0x" << std::hex << entry_point;
    uintptr_t result;
    if (!jit.ExecuteFunction("main", &result)) {
        LOG(ERROR) << "Failed to execute lifted code at IP: 0x" << std::hex << entry_point;
        return false;
    }
    VLOG(1) << "Successfully executed lifted code at IP: 0x" << std::hex << entry_point;
    LOG(INFO) << "Result: " << result;

    // Process any new missing memory
    bool missing_memory_found = false;
    const auto &new_missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
    VLOG(1) << "New missing memory encountered (" << new_missing_memory.size() << "):\n";
    for (const auto &mem : new_missing_memory) {
        VLOG(1) << "  " << std::hex << mem.first << ", size: " << mem.second << " bytes";
        missing_memory_found = true;
    }
    
    VLOG(1) << "Taking only first missing memory, if it's not already in missing_memory...";
    for (const auto &mem : new_missing_memory) {
        bool found = false;
        for (const auto& addr : added_memory) {
            if (addr.first == mem.first) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG(INFO) << "Adding missing memory: " << std::hex << mem.first << ", size: " << mem.second << " bytes";
            missing_memory.push_back(mem);
            added_memory.push_back(mem);
            break; // todo: push only first missing memory
        }
    }
    Runtime::MissingMemoryTracker::ClearMissingMemory();

    // Process any new missing blocks
    const auto &new_missing_blocks = Runtime::MissingBlockTracker::GetMissingBlocks();
    if (!missing_memory_found && new_missing_blocks.size() > 0) {
        // Print all missing blocks encountered during execution
        LOG(INFO) << "New missing blocks encountered (" << new_missing_blocks.size() << "):\n";
        for (const auto &pc : new_missing_blocks) {
            LOG(INFO) << "  " << std::hex << pc;
            missing_blocks.push_back(pc);
        }
    }
    Runtime::MissingBlockTracker::ClearMissingBlocks();

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
        std::string entry_point_name;
        Recycle::setupEnvironment(llvm_context, missing_blocks, entry_point, options.getStopAddr(), entry_point_name);

        // Data tracking structures
        std::vector<std::pair<uint64_t, std::string>> addr_to_func_map;
        std::vector<std::pair<uint64_t, uint8_t>> missing_memory;
        std::vector<std::pair<uint64_t, uint8_t>> added_memory;
        std::vector<std::unique_ptr<llvm::Module>> lifted_modules;
        uint64_t ip = 0;
        size_t iteration_count = 0;


        // Main processing loop
        while ((missing_blocks.size() > 0 || missing_memory.size() > 0) && 
               iteration_count < options.getMaxTranslations()) {
            LOG(INFO) << std::endl;

            if (missing_blocks.size() > 0) {
                // Process a missing block
                ip = missing_blocks.back();
                missing_blocks.pop_back();
                
                // First lift the basic block
                std::unique_ptr<llvm::Module> lifted_module;
                if (!Recycle::liftBasicBlock(lifted_module, memory_reader, *llvm_context, ip)) {
                    LOG(ERROR) << "Failed to lift basic block at IP: 0x" << std::hex << ip;
                    return 1;
                }
                // write lifted module to file
                const auto filename_prefix = Recycle::getFilenamePrefix("lifted", iteration_count);
                BitcodeManipulation::DumpModule(*lifted_module, filename_prefix);

                lifted_modules.push_back(std::move(lifted_module));
            }
            // todo: refactoring artifact 
            if (missing_memory.size() > 0) {
                missing_memory.clear();
            }

            // Merge all lifted modules into new memory module
            std::unique_ptr<llvm::Module> merged_module = std::make_unique<llvm::Module>("merged_module", *llvm_context);
            for (const auto& module : lifted_modules) {
                BitcodeManipulation::MergeModules(*merged_module, *module);
            }

            // Then process the lifted block
            if (!Recycle::prepareBlockForRun(merged_module, addr_to_func_map, memory_reader,
                                             ip, entry_point, entry_point_name))
            {
                LOG(ERROR) << "Failed to prepare block for run at IP: 0x" << std::hex << ip;
                return 1;
            }

            // Process missing memory
            if (!Recycle::processMissingMemory(merged_module, added_memory, memory_reader)) {
                LOG(ERROR) << "Failed to process missing memory";
                return 1;
            }

            // Create get saved memory ptr function
            if (BitcodeManipulation::CreateGetSavedMemoryPtr(*merged_module) == nullptr) {
                LOG(ERROR) << "Failed to create get saved memory ptr";
                return 1;
            }

            // optimize module
            const auto exclustion = std::vector<std::string>{"main"};
            BitcodeManipulation::ReplaceFunction(*merged_module, "__remill_write_memory_64", "__remill_write_memory_64_opt");
            BitcodeManipulation::RemoveOptNoneAttribute(*merged_module, exclustion);
            BitcodeManipulation::MakeSymbolsInternal(*merged_module, exclustion);
            BitcodeManipulation::MakeFunctionsInline(*merged_module, exclustion);
            BitcodeManipulation::DumpModule(*merged_module, Recycle::getFilenamePrefix("opt_pre", iteration_count));
            BitcodeManipulation::OptimizeModule(*merged_module, 3); // Calling optimize module twice is intentional
            BitcodeManipulation::DumpModule(*merged_module, Recycle::getFilenamePrefix("opt", iteration_count));
            BitcodeManipulation::OptimizeModule(*merged_module, 3); // Calling optimize module twice is intentional
            BitcodeManipulation::DumpModule(*merged_module, Recycle::getFilenamePrefix("opt2", iteration_count));

            // Execute JIT code
            const auto filename_prefix = Recycle::getFilenamePrefix("merged", iteration_count);
            if (!Recycle::executeJITCode(merged_module, ip, entry_point, filename_prefix, 
                                       missing_memory, added_memory, missing_blocks)) {
                return 1;
            }
            
            iteration_count++;
            //if (iteration_count > 2) {
            //    break;
            //}
        }

        LOG(INFO) << "Program lifted successfully, " << iteration_count << " iterations completed";

        //// create arrow function for RuntimeCallback
        //auto runtime_callback = [](void* s, uint64_t* pc, void** memory) {
        //    static uint64_t rcx = 0;
        //    //LOG(INFO) << "Runtime callback, rcx: " << rcx;
        //    X86State* state = static_cast<X86State*>(s);
        //    state->gpr.rcx.dword = rcx++;
        //};

        //auto callback = [](uint64_t result) {
        //    LOG(INFO) << "Bruteforce callback, result: " << result;
        //    return result == 1;
        //};

        //Recycle::bruteforceJITCode(saved_module, ip, entry_point, missing_memory, added_memory, missing_blocks, runtime_callback, callback);
        // Optimize the module now, first replace Utils functions with optimized versions
        // BitcodeManipulation::ReplaceFunction(*saved_module, "__remill_write_memory_64", "__remill_write_memory_64_opt");
        //BitcodeManipulation::ReplaceFunction(*saved_module, "SetStack", "SetStack_opt");
        //BitcodeManipulation::ReplaceFunction(*saved_module, "ReadGlobalMemoryEdgeChecked_64", "ReadGlobalMemoryEdgeChecked_64_opt");
        //BitcodeManipulation::ReplaceFunction(*saved_module, "ReadGlobalMemoryEdgeChecked_32", "ReadGlobalMemoryEdgeChecked_32_opt");
        //BitcodeManipulation::ReplaceFunction(*saved_module, "ReadGlobalMemoryEdgeChecked_16", "ReadGlobalMemoryEdgeChecked_16_opt");
        //BitcodeManipulation::ReplaceFunction(*saved_module, "ReadGlobalMemoryEdgeChecked_8", "ReadGlobalMemoryEdgeChecked_8_opt");

        return 0;
    }
    catch (const std::exception& e) {
        LOG(ERROR) << "Exception: " << e.what();
        return 1;
    }
} 