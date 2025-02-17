#pragma once

#include <cstdint>

namespace Runtime {

// Sample runtime functions that will be linked with lifted code
extern "C" {
    // Memory operations
    uint64_t ReadMemory64(uint64_t addr);
    void WriteMemory64(uint64_t addr, uint64_t value);
    
    // Register operations
    uint64_t GetRegister(uint32_t reg_id);
    void SetRegister(uint32_t reg_id, uint64_t value);
    
    // Flag operations
    bool GetFlag(uint32_t flag_id);
    void SetFlag(uint32_t flag_id, bool value);

    // Remill intrinsics
    void* __remill_missing_block(void* state, uint64_t pc, void* memory);
    void* __remill_write_memory_64(void* memory, uint64_t addr, uint64_t value);
    void __remill_log_function(const char* func_name);
    void __sub_1400016d0(void* state, uint64_t pc, void* memory);
}

} // namespace Runtime 