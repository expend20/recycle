#include <remill/Arch/X86/Runtime/State.h>
#include "../JIT/JITRuntime.h"
#include "Utils.h"
#include <string.h>
#include <type_traits>

#ifdef LOG_ENABLED
#define LOG_MESSAGE(...) Runtime::LogMessage(__VA_ARGS__)
#else
#define LOG_MESSAGE(...)
#endif

extern "C" {

void* __remill_missing_block(void* state, uint64_t pc, void* memory);
void* main_next(void* state, uint64_t pc, void* memory);

size_t StartPC;
size_t GSBase;
uintptr_t Stack;

// initial values
uint64_t GlobalRcx = 0;

void* Memory = nullptr;

__attribute__((always_inline)) void SetParameters(X86State& state)
{
    // make configurable
    state.gpr.rcx.qword = GlobalRcx;
};

__attribute__((always_inline)) void SetPC(X86State& state, uint64_t pc)
{
    state.gpr.rip.qword = pc;
}

__attribute__((always_inline)) void SetStack(X86State& state, uintptr_t stack)
{
    state.gpr.rsp.qword = stack;
}

__attribute__((always_inline)) void SetGSBase(X86State& state, uint64_t gs)
{
    state.addr.gs_base.qword = gs;
}

int main() {
    X86State State = {};

    SetParameters(State);
    SetPC(State, StartPC);
    SetStack(State, Stack);
    SetGSBase(State, GSBase);
    main_next(&State, StartPC, Memory);
    return (int)State.gpr.rax.dword;
}

__attribute__((always_inline)) bool __remill_flag_computation_carry(bool result, ...) {
    return result;
}

__attribute__((always_inline)) bool __remill_flag_computation_zero(bool result, ...) {
    return result;
}

__attribute__((always_inline)) bool __remill_flag_computation_sign(bool result, ...) {
    return result;
}

__attribute__((always_inline)) bool __remill_flag_computation_overflow(bool result, ...) {
    return result;
}

__attribute__((always_inline)) void* __remill_jump(void *state, addr_t addr, void* memory) {
    return __remill_missing_block(state, addr, memory);
}

__attribute__((always_inline)) void* __remill_function_return(void *state, addr_t addr, void* memory) {
    return __remill_missing_block(state, addr, memory);
}

__attribute__((always_inline)) uint8_t __remill_undefined_8(void) {
    return 0;
}

__attribute__((always_inline)) uint16_t __remill_undefined_16(void) {
    return 0;
}

__attribute__((always_inline)) uint32_t __remill_undefined_32(void) {  
    return 0;
}

__attribute__((always_inline)) uint64_t __remill_undefined_64(void) {
    return 0;
}

__attribute__((always_inline)) bool __remill_compare_neq(bool result) {
    return result;
}

} // extern "C"

