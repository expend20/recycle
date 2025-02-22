#include <remill/Arch/X86/Runtime/State.h>
#include "../JIT/JITRuntime.h"

extern "C" {

uint8_t Stack[0x100000];
const uint64_t StackBase = (uint64_t)Stack;
const uint64_t StackSize = sizeof(Stack);

X86State State = {};

// initial values
uint64_t GlobalRcx = 0;
uint64_t GlobalPC = 0;

void* Memory = nullptr;

void SetParameters()
{
    // make configurable
    State.gpr.rcx.qword = GlobalRcx;
};

void SetPC(uint64_t pc)
{
    Runtime::LogMessage("[Utils] SetPC: 0x%lx", pc);
    GlobalPC = pc;
    State.gpr.rip.qword = pc;
}

void SetStack()
{
    Runtime::LogMessage("[Utils] SetStack: [0x%lx:0x%lx], size: 0x%lx", StackBase, StackBase + StackSize, StackSize);
    State.gpr.rsp.qword = StackBase + StackSize;
}

void SetGSBase(uint64_t gs)
{
    State.addr.gs_base.qword = gs;
}

void InitializeX86AddressSpace(
    X86State *state, uint64_t ss, uint64_t es, uint64_t gs, uint64_t fs, uint64_t ds, uint64_t cs)
{
    state->addr.cs_base.qword = cs;
    state->addr.ds_base.qword = ds;
    state->addr.es_base.qword = es;
    state->addr.ss_base.qword = ss;
    state->addr.gs_base.qword = gs;
    state->addr.fs_base.qword = fs;
}

void* __remill_write_memory_64(void *memory, addr_t addr, uint64_t val) {
    if (addr >= StackBase && addr < StackBase + StackSize) {
        Runtime::LogMessage("[Utils] __remill_write_memory_64 stack: [0x%lx] = 0x%lx", addr, val);
        *(uint64_t*)addr = val;
        return memory;
    }
    Runtime::LogMessage("[Utils] __remill_write_memory_64 write out of stack");
    exit(1);
    return memory;
}

// __remill_read_memory_64
uint64_t __remill_read_memory_64(void *memory, addr_t addr) {
    if (addr >= StackBase && addr < StackBase + StackSize) {
        const uint64_t val = *(uint64_t*)addr;
        Runtime::LogMessage("[Utils] __remill_read_memory_64 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }
    Runtime::LogMessage("[Utils] __remill_read_memory_64 read out of stack 0x%lx", addr);
    exit(1);
    return 0;
}

// __remill_read_memory_32
uint32_t __remill_read_memory_32(void *memory, addr_t addr) {
    if (addr >= StackBase && addr < StackBase + StackSize) {
        const uint32_t val = *(uint32_t*)addr;
        Runtime::LogMessage("[Utils] __remill_read_memory_32 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }
    Runtime::LogMessage("[Utils] __remill_read_memory_32 read out of stack 0x%lx", addr);
    exit(1);
    return 0;
}

// __remill_flag_computation_carry
bool __remill_flag_computation_carry(bool result, ...) {
    Runtime::LogMessage("[Utils] __remill_flag_computation_carry: %d", result);
    return result;
}

// __remill_flag_computation_zero
bool __remill_flag_computation_zero(bool result, ...) {
    Runtime::LogMessage("[Utils] __remill_flag_computation_zero: %d", result);
    return result;
}

// __remill_flag_computation_sign
bool __remill_flag_computation_sign(bool result, ...) {
    Runtime::LogMessage("[Utils] __remill_flag_computation_sign: %d", result);
    return result;
}

// __remill_flag_computation_overflow
bool __remill_flag_computation_overflow(bool result, ...) {
    Runtime::LogMessage("[Utils] __remill_flag_computation_overflow: %d", result);
    return result;
}

void* __remill_missing_block(void* state, uint64_t pc, void* memory);

void* __remill_jump(void *state, addr_t addr, void* memory) {
    Runtime::LogMessage("[Utils] __remill_jump: 0x%lx", addr);
    return __remill_missing_block(state, addr, memory);
}

} // extern "C"

