#include "JITRuntime.h"
#include <glog/logging.h>
#include <unordered_map>

namespace Runtime {

// Initialize static members
std::vector<uint64_t> MissingBlockTracker::missing_blocks;
std::unordered_set<uint64_t> MissingBlockTracker::ignored_addresses;

void MissingBlockTracker::AddMissingBlock(uint64_t pc) {
    if (!IsAddressIgnored(pc)) {
        LOG(INFO) << "Adding missing block at PC: 0x" << std::hex << pc;
        missing_blocks.push_back(pc);
    }
    else {
        LOG(INFO) << "Ignoring missing block at PC: 0x" << std::hex << pc;
    }
}

void MissingBlockTracker::AddIgnoredAddress(uint64_t pc) {
    ignored_addresses.insert(pc);
}

void MissingBlockTracker::RemoveIgnoredAddress(uint64_t pc) {
    ignored_addresses.erase(pc);
}

void MissingBlockTracker::ClearIgnoredAddresses() {
    ignored_addresses.clear();
}

bool MissingBlockTracker::IsAddressIgnored(uint64_t pc) {
    return ignored_addresses.find(pc) != ignored_addresses.end();
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

void* __remill_async_hyper_call(void* memory, uint64_t addr, uint64_t value) {
    LOG(INFO) << "Async hyper call at address: 0x" << std::hex << addr 
              << " with value: 0x" << value;
    return memory;
}

} // extern "C"

} // namespace Runtime 
