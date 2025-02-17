#include "JITRuntime.h"
#include <glog/logging.h>
#include <unordered_map>

namespace Runtime {

// Initialize static member
std::vector<uint64_t> MissingBlockTracker::missing_blocks;

void MissingBlockTracker::AddMissingBlock(uint64_t pc) {
    missing_blocks.push_back(pc);
}

const std::vector<uint64_t>& MissingBlockTracker::GetMissingBlocks() {
    return missing_blocks;
}

void MissingBlockTracker::ClearMissingBlocks() {
    missing_blocks.clear();
}

extern "C" {

void* __remill_missing_block(void* state, uint64_t pc, void* memory) {
    LOG(INFO) << "Missing block at PC: 0x" << std::hex << pc;
    MissingBlockTracker::AddMissingBlock(pc);
    return memory;
}

void* __remill_write_memory_64(void* memory, uint64_t addr, uint64_t value) {
    LOG(INFO) << "Writing memory at address: 0x" << std::hex << addr 
              << " value: 0x" << value;
    return memory;
}

void __remill_log_function(const char* func_name) {
    LOG(INFO) << "Executing function: " << func_name;
}

} // extern "C"

} // namespace Runtime 
