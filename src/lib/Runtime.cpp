#include "Runtime.h"
#include <glog/logging.h>
#include <unordered_map>

namespace {
    // Simple memory simulation for testing
    std::unordered_map<uint64_t, uint64_t> g_memory;
    std::unordered_map<uint32_t, uint64_t> g_registers;
    std::unordered_map<uint32_t, bool> g_flags;
}

namespace Runtime {

extern "C" {

uint64_t ReadMemory64(uint64_t addr) {
    LOG(INFO) << "Reading memory at address: 0x" << std::hex << addr;
    return g_memory[addr];
}

void WriteMemory64(uint64_t addr, uint64_t value) {
    LOG(INFO) << "Writing memory at address: 0x" << std::hex << addr 
              << " value: 0x" << value;
    g_memory[addr] = value;
}

uint64_t GetRegister(uint32_t reg_id) {
    LOG(INFO) << "Reading register: " << reg_id;
    return g_registers[reg_id];
}

void SetRegister(uint32_t reg_id, uint64_t value) {
    LOG(INFO) << "Setting register: " << reg_id << " to value: 0x" 
              << std::hex << value;
    g_registers[reg_id] = value;
}

bool GetFlag(uint32_t flag_id) {
    LOG(INFO) << "Reading flag: " << flag_id;
    return g_flags[flag_id];
}

void SetFlag(uint32_t flag_id, bool value) {
    LOG(INFO) << "Setting flag: " << flag_id << " to value: " << value;
    g_flags[flag_id] = value;
}

void* __remill_missing_block(void* state, uint64_t pc, void* memory) {
    LOG(INFO) << "Missing block at PC: 0x" << std::hex << pc;
    return memory;
}

void* __remill_write_memory_64(void* memory, uint64_t addr, uint64_t value) {
    LOG(INFO) << "Writing memory at address: 0x" << std::hex << addr 
              << " value: 0x" << value;
    g_memory[addr] = value;
    return memory;
}

void __remill_log_function(const char* func_name) {
    LOG(INFO) << "Executing function: " << func_name;
}

} // extern "C"

} // namespace Runtime 