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

// Sample runtime functions that will be linked with lifted code
extern "C" {
    // Remill intrinsics
    void* __remill_missing_block_final(void* state, uint64_t pc, void* memory);
    void* __remill_log_write_memory_64(void* memory, uintptr_t addr, uint64_t val);
    void* __remill_async_hyper_call(void* state, uint64_t pc, void* memory);
    void __remill_log_function(const char* func_name, uint64_t pc);
    
    // Variadic logging function
    void LogMessage(const char* format, ...);
}

} // namespace Runtime 