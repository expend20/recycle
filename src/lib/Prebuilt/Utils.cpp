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
    Runtime::LogMessage("[pc] SetPC: 0x%lx", pc);
    GlobalPC = pc;
    State.gpr.rip.qword = pc;
}

void SetStack()
{
    Runtime::LogMessage("[stack] SetStack: [0x%lx:0x%lx], size: 0x%lx", StackBase, StackBase + StackSize, StackSize);
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
        Runtime::LogMessage("[stack] __remill_write_memory_64: [0x%lx] = 0x%lx", addr, val);
        *(uint64_t*)addr = val;
        return memory;
    }
    return memory;
}

} // extern "C"

