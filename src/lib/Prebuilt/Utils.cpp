#include <remill/Arch/X86/Runtime/State.h>
#include "../JIT/JITRuntime.h"
#include "Utils.h"

extern "C" {

void* __remill_missing_block(void* state, uint64_t pc, void* memory);
void* __rt_get_saved_memory_ptr(uintptr_t addr);

uint8_t Stack[PREBUILT_STACK_SIZE];
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

typedef struct {
    uint64_t addr;
    uint8_t val[PREBUILT_MEMORY_CELL_SIZE];
} MemoryCell64;

extern MemoryCell64 GlobalMemoryCells64[] = {
    {0x1234567890, {0}},
    //{0x3e2060, 0x003e1000},
    //{0x3e1010, 0x140000000},
    //{0x14001678f, 0x0161010000016101},
};

// __remill_read_memory_64
uint64_t __remill_read_memory_64(void *memory, addr_t addr) {
    if (addr >= StackBase && addr < StackBase + StackSize) {
        const uint64_t val = *(uint64_t*)addr;
        Runtime::LogMessage("[Utils] __remill_read_memory_64 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }
    Runtime::LogMessage("[Utils] __remill_read_memory_64 checking global memory cells");
    void* saved_memory = __rt_get_saved_memory_ptr(addr);
    if (saved_memory) {
        Runtime::LogMessage("[Utils] __remill_read_memory_64 found saved memory at 0x%lx, value: 0x%lx", addr, *(uint64_t*)saved_memory);
        return *(uint64_t*)saved_memory;
    }
    Runtime::LogMessage("[Utils] __remill_read_memory_64 requesting memory read at 0x%lx", addr);
    return Runtime::__rt_read_memory64(memory, addr);
}

// __remill_read_memory_32
uint32_t __remill_read_memory_32(void *memory, addr_t addr) {
    if (addr >= StackBase && addr < StackBase + StackSize) {
        const uint32_t val = *(uint32_t*)addr;
        Runtime::LogMessage("[Utils] __remill_read_memory_32 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }
    void* saved_memory = __rt_get_saved_memory_ptr(addr);
    if (saved_memory) {
        Runtime::LogMessage("[Utils] __remill_read_memory_32 found saved memory at 0x%lx, value: 0x%lx", addr, *(uint32_t*)saved_memory);
        return *(uint32_t*)saved_memory;
    }
    Runtime::LogMessage("[Utils] __remill_read_memory_32 requesting memory read at 0x%lx", addr);
    return Runtime::__rt_read_memory32(memory, addr);
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

void* __remill_jump(void *state, addr_t addr, void* memory) {
    Runtime::LogMessage("[Utils] __remill_jump: 0x%lx", addr);
    return __remill_missing_block(state, addr, memory);
}

} // extern "C"

