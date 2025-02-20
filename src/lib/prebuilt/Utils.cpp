#include "remill/Arch/X86/Runtime/State.h"

extern "C" {

uint64_t StackBase;
uint64_t StackSize;

void InitializeX86StateGPR(State *state, const GPR &gpr) {
    state->gpr.rax.qword = gpr.rax.qword;
    state->gpr.rbx.qword = gpr.rbx.qword;
    state->gpr.rcx.qword = gpr.rcx.qword;
    state->gpr.rdx.qword = gpr.rdx.qword;
    state->gpr.rsi.qword = gpr.rsi.qword;
    state->gpr.rdi.qword = gpr.rdi.qword;
    state->gpr.rsp.qword = gpr.rsp.qword;
    state->gpr.rbp.qword = gpr.rbp.qword;
    state->gpr.rip.qword = gpr.rip.qword;
    state->gpr.r8.qword = gpr.r8.qword;
    state->gpr.r9.qword = gpr.r9.qword;
    state->gpr.r10.qword = gpr.r10.qword;
    state->gpr.r11.qword = gpr.r11.qword;
    state->gpr.r12.qword = gpr.r12.qword;
    state->gpr.r13.qword = gpr.r13.qword;
    state->gpr.r14.qword = gpr.r14.qword;
    state->gpr.r15.qword = gpr.r15.qword;
};

void InitializeX86AddressSpace(State *state, const AddressSpace &address_space) {
    state->addr.gs_base.qword = address_space.gs_base.qword;
    state->addr.es_base.qword = address_space.es_base.qword;
    state->addr.ds_base.qword = address_space.ds_base.qword;
    state->addr.cs_base.qword = address_space.cs_base.qword;
}

} // extern "C"

