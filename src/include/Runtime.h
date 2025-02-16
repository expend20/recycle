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
}

// State structure matching the LLVM IR
struct alignas(8) State {
    struct X86State {
        struct ArchState {
            uint32_t padding[2];
            uint64_t rflag;
        } arch_state;
        
        uint8_t vector_regs[32 * 64];  // 32 vector registers * 64 bytes each
        uint8_t arith_flags[16];       // ArithFlags
        uint64_t seg_cs;               // Code segment
        uint8_t segments[96];          // Other segments
        uint8_t address_space[96];     // AddressSpace
        
        struct GPR {
            uint64_t rax;
            uint64_t padding1;
            uint64_t rbx;
            uint64_t padding2;
            uint64_t rcx;
            uint64_t padding3;
            uint64_t rdx;
            uint64_t padding4;
            uint64_t rsi;
            uint64_t padding5;
            uint64_t rdi;
            uint64_t padding6;
            uint64_t rbp;
            uint64_t padding7;
            uint64_t rsp;
            uint64_t padding8;
            uint64_t r8;
            uint64_t padding9;
            uint64_t r9;
            uint64_t padding10;
            uint64_t r10;
            uint64_t padding11;
            uint64_t r11;
            uint64_t padding12;
            uint64_t r12;
            uint64_t padding13;
            uint64_t r13;
            uint64_t padding14;
            uint64_t r14;
            uint64_t padding15;
            uint64_t r15;
            uint64_t padding16;
            uint64_t rip;
            uint64_t padding17;
        } gpr;

        uint8_t remaining[1024];  // Rest of the state (FPU, MMX, etc.)
    } x86_state;
};

} // namespace Runtime 