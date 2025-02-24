#include "JITRuntime.h"

#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

#include <glog/logging.h>

namespace Runtime {

// Initialize static members
std::vector<uint64_t> MissingBlockTracker::missing_blocks;
std::unordered_set<uint64_t> MissingBlockTracker::ignored_addresses;
std::vector<std::pair<uint64_t, uint8_t>> MissingMemoryTracker::missing_memory;

extern "C" {

void LogMessage(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    LOG(INFO) << "JRT: " << buffer;
}

void* __rt_missing_block(void* state, uint64_t pc, void* memory) {
    LOG(INFO) << "JRT: Missing block at PC: 0x" << std::hex << pc;
    MissingBlockTracker::AddMissingBlock(pc);
    return memory;
}

void* __remill_async_hyper_call(void* state, uint64_t pc, void* memory) {
    LOG(INFO) << "JRT: Async hyper call at address: 0x" << std::hex << pc;
    // TODO: Implement async hyper call
    exit(1);
    return memory;
}

uint64_t __rt_read_memory64(void *memory, intptr_t addr) {
    LOG(INFO) << "JRT: Reading memory at address: 0x" << std::hex << addr;
    MissingMemoryTracker::AddMissingMemory(addr, 8);
    return 0;
}

void* __rt_write_memory64(void *memory, intptr_t addr, uint64_t val) {
    LOG(INFO) << "JRT: Writing memory at address: 0x" << std::hex << addr << " with value: 0x" << std::hex << val;
    MissingMemoryTracker::AddMissingMemory(addr, 8);
    return memory;
}

uint32_t __rt_read_memory32(void *memory, intptr_t addr) {
    LOG(INFO) << "JRT: Reading memory at address: 0x" << std::hex << addr;
    MissingMemoryTracker::AddMissingMemory(addr, 4);
    return 0;
}

void* __rt_write_memory32(void *memory, intptr_t addr, uint32_t val) {
    LOG(INFO) << "JRT: Writing memory at address: 0x" << std::hex << addr << " with value: 0x" << std::hex << val;
    MissingMemoryTracker::AddMissingMemory(addr, 4);
    return memory;
}

uint16_t __rt_read_memory16(void *memory, intptr_t addr) {
    LOG(INFO) << "JRT: Reading memory at address: 0x" << std::hex << addr;
    MissingMemoryTracker::AddMissingMemory(addr, 2);
    return 0;
}

void* __rt_write_memory16(void *memory, intptr_t addr, uint16_t val) {
    LOG(INFO) << "JRT: Writing memory at address: 0x" << std::hex << addr << " with value: 0x" << std::hex << val;
    MissingMemoryTracker::AddMissingMemory(addr, 2);
    return memory;
}

uint8_t __rt_read_memory8(void *memory, intptr_t addr) {
    LOG(INFO) << "JRT: Reading memory at address: 0x" << std::hex << addr;
    MissingMemoryTracker::AddMissingMemory(addr, 1);
    return 0;
}

void* __rt_write_memory8(void *memory, intptr_t addr, uint8_t val) {
    LOG(INFO) << "JRT: Writing memory at address: 0x" << std::hex << addr << " with value: 0x" << std::hex << val;
    MissingMemoryTracker::AddMissingMemory(addr, 1);
    return memory;
}

} // extern "C"

void MissingBlockTracker::AddMissingBlock(uint64_t pc) {
    if (!IsAddressIgnored(pc)) {
        // Check if block is not already in missing_blocks before adding
        if (std::find(missing_blocks.begin(), missing_blocks.end(), pc) == missing_blocks.end()) {
            LOG(INFO) << "JRT: Adding missing block at PC: 0x" << std::hex << pc;
            missing_blocks.push_back(pc);
        }
    }
    else {
        LOG(INFO) << "JRT: Ignoring missing block at PC: 0x" << std::hex << pc;
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

void MissingMemoryTracker::AddMissingMemory(uint64_t addr, uint8_t size) {
    missing_memory.push_back(std::make_pair(addr, size));
}

const std::vector<std::pair<uint64_t, uint8_t>>& MissingMemoryTracker::GetMissingMemory() {
    return missing_memory;
}

void MissingMemoryTracker::ClearMissingMemory() {
    missing_memory.clear();
}

} // namespace Runtime 
