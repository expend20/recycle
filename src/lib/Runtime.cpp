#include "Runtime.h"
#include <glog/logging.h>
#include <unordered_map>

namespace Runtime {

extern "C" {

void* __remill_missing_block(void* state, uint64_t pc, void* memory) {
    LOG(INFO) << "Missing block at PC: 0x" << std::hex << pc;
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
