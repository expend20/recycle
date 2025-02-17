#pragma once

#include <cstdint>

namespace Runtime {

// Sample runtime functions that will be linked with lifted code
extern "C" {
    // Remill intrinsics
    void* __remill_missing_block(void* state, uint64_t pc, void* memory);
    void* __remill_write_memory_64(void* memory, uint64_t addr, uint64_t value);
    void __remill_log_function(const char* func_name);
}

} // namespace Runtime 