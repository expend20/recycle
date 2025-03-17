#pragma once

#include <cstdint>
#include <vector>
#include <unordered_set>

namespace Runtime {

class MissingBlockTracker {
public:
    static void AddMissingBlock(uint64_t pc);
    static const std::vector<uint64_t>& GetMissingBlocks();
    static void ClearMissingBlocks();
    static void AddIgnoredAddress(uint64_t pc);
    static void RemoveIgnoredAddress(uint64_t pc);
    static void ClearIgnoredAddresses();
    static bool IsAddressIgnored(uint64_t pc);

private:
    static std::vector<uint64_t> missing_blocks;
    static std::unordered_set<uint64_t> ignored_addresses;
};

class MissingMemoryTracker {
public:
    static void AddMissingMemory(uint64_t addr, uint8_t size);
    static const std::vector<std::pair<uint64_t, uint8_t>>& GetMissingMemory();
    static void ClearMissingMemory();

private:
    static std::vector<std::pair<uint64_t, uint8_t>> missing_memory;
};

// Add this before the extern "C" block
using RuntimeCallbackFn = void(*)(void* state, uint64_t* pc, void** memory);

void RegisterRuntimeCallback(RuntimeCallbackFn callback);
void UnregisterRuntimeCallback();

// Sample runtime functions that will be linked with lifted code
extern "C" {
    // Remill intrinsics
    void* __rt_missing_block(void* state, uint64_t pc, void* memory);
    uint64_t __rt_read_memory64(void *memory, intptr_t addr);
    uint32_t __rt_read_memory32(void *memory, intptr_t addr);
    uint16_t __rt_read_memory16(void *memory, intptr_t addr);
    uint8_t __rt_read_memory8(void *memory, intptr_t addr);
    void* __rt_write_memory64(void *memory, intptr_t addr, uint64_t val);
    void* __rt_write_memory32(void *memory, intptr_t addr, uint32_t val);
    void* __rt_write_memory16(void *memory, intptr_t addr, uint16_t val);
    void* __rt_write_memory8(void *memory, intptr_t addr, uint8_t val);

    void* __remill_async_hyper_call(void* state, uint64_t pc, void* memory);
    // Variadic logging function
    void LogMessage(const char* format, ...);
    void RuntimeCallback(void* state, uint64_t* pc, void** memory);
    void RuntimeExit(uint32_t code);
}

} // namespace Runtime 