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

// Runtime assistance functions
extern "C" {
void* __remill_missing_block(void* state, uint64_t pc, void* memory);
void* __rt_get_saved_memory_ptr(uintptr_t addr);
}


template<typename T>
T ReadGlobalMemoryEdgeChecked(void *memory, addr_t addr) {
    static_assert(std::is_integral<T>::value, "T must be an integral type");
    const size_t size = sizeof(T);

    // Get the memory page for the address
    void* page = __rt_get_saved_memory_ptr(addr);
    if (!page) {
        // No valid page found, redirect to runtime read
        LOG_MESSAGE("[Utils] ReadGlobalMemoryEdgeChecked: No valid page at 0x%lx, redirecting to runtime", addr);
        
        // Call appropriate runtime read function based on size
        if (sizeof(T) == 8) {
            return static_cast<T>(Runtime::__rt_read_memory64(memory, addr));
        } else if (sizeof(T) == 4) {
            return static_cast<T>(Runtime::__rt_read_memory32(memory, addr));
        } else if (sizeof(T) == 2) {
            return static_cast<T>(Runtime::__rt_read_memory16(memory, addr));
        } else if (sizeof(T) == 1) {
            return static_cast<T>(Runtime::__rt_read_memory8(memory, addr));
        } else {
            // Fallback for any other size (shouldn't happen with our constraints)
            Runtime::LogMessage("[Utils] ReadGlobalMemoryEdgeChecked: Unsupported size: %zu", size);
            return 0;
        }
    }

    // Calculate page boundary and check if access crosses it
    addr_t page_base = (addr / PREBUILT_MEMORY_CELL_SIZE) * PREBUILT_MEMORY_CELL_SIZE;
    addr_t page_end = page_base + PREBUILT_MEMORY_CELL_SIZE;
    
    // If access doesn't cross page boundary, simply dereference and return
    if (addr + size <= page_end) {
        // Copy the bytes from memory to result
        uint8_t* src = (uint8_t*)page + (addr - page_base);
        T result = *(T*)src;
        LOG_MESSAGE("[Utils] ReadGlobalMemoryEdgeChecked: Read 0x%lx from addr 0x%lx (size: %zu)", 
                          (uint64_t)result, addr, size);
        return result;
    }
    
    // Handle cross-page boundary access
    LOG_MESSAGE("[Utils] ReadGlobalMemoryEdgeChecked: Access crosses page boundary at 0x%lx, size: %zu", addr, size);
    
    // Calculate bytes in first and second page
    size_t first_page_bytes = page_end - addr;
    size_t second_page_bytes = size - first_page_bytes;
    
    // Get the second page
    addr_t second_page_addr = page_end;
    void* second_page = __rt_get_saved_memory_ptr(second_page_addr);
    
    if (!second_page) {
        // Second page is not available, redirect to runtime
        LOG_MESSAGE("[Utils] ReadGlobalMemoryEdgeChecked: Second page not available, redirecting to runtime");
        
        // Call appropriate runtime read function based on size
        if (sizeof(T) == 8) {
            return static_cast<T>(Runtime::__rt_read_memory64(memory, addr));
        } else if (sizeof(T) == 4) {
            return static_cast<T>(Runtime::__rt_read_memory32(memory, addr));
        } else if (sizeof(T) == 2) {
            return static_cast<T>(Runtime::__rt_read_memory16(memory, addr));
        } else if (sizeof(T) == 1) {
            return static_cast<T>(Runtime::__rt_read_memory8(memory, addr));
        } else {
            // Fallback for any other size (shouldn't happen with our constraints)
            LOG_MESSAGE("[Utils] ReadGlobalMemoryEdgeChecked: Unsupported size: %zu", size);
            return 0;
        }
    }
    
    // Both pages are valid, read byte by byte and combine
    T result = 0;
    
    // Read bytes from first page
    uint8_t* first_src = (uint8_t*)page + (addr - page_base);
    memcpy(&result, first_src, first_page_bytes);
    
    // Read bytes from second page
    uint8_t* second_src = (uint8_t*)second_page;
    memcpy((uint8_t*)&result + first_page_bytes, second_src, second_page_bytes);
    
    LOG_MESSAGE("[Utils] ReadGlobalMemoryEdgeChecked: Cross-boundary read 0x%lx from addr 0x%lx (size: %zu)", 
                       (uint64_t)result, addr, size);
    
    return result;
}

extern "C" {

size_t Stack;

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
    LOG_MESSAGE("[Utils] SetPC: 0x%lx", pc);
    GlobalPC = pc;
    State.gpr.rip.qword = pc;
}

void SetStack()
{
    //LOG_MESSAGE("[Utils] SetStack: [0x%lx:0x%lx], size: 0x%lx", StackBase, StackBase + StackSize, StackSize);
    //State.gpr.rsp.qword = StackBase + StackSize - 8;
    //*((uint64_t*)(StackBase + StackSize) - 8) = 0x1234567890;
    State.gpr.rsp.qword = PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE - 8;
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

/*
void* __remill_write_memory_64(void *memory, addr_t addr, uint64_t val) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        LOG_MESSAGE("[Utils] __remill_write_memory_64 stack: [0x%lx] = 0x%lx", addr, val);
        const auto offset = addr - PREBUILT_STACK_BASE;
        *(uint64_t*)(Stack + offset) = val;
        return memory;
    }
    LOG_MESSAGE("[Utils] __remill_write_memory_64 write out of stack addr: 0x%lx, val: 0x%lx", addr, val);
    exit(1);
    return memory;
}

void* __remill_write_memory_32(void *memory, addr_t addr, uint32_t val) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        LOG_MESSAGE("[Utils] __remill_write_memory_32 stack: [0x%lx] = 0x%lx", addr, val);
        const auto offset = addr - PREBUILT_STACK_BASE;
        *(uint32_t*)(Stack + offset) = val;
        return memory;
    }
    LOG_MESSAGE("[Utils] __remill_write_memory_32 write out of stack addr: 0x%lx, val: 0x%lx", addr, val);
    exit(1);
    return memory;
}

void* __remill_write_memory_16(void *memory, addr_t addr, uint16_t val) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        LOG_MESSAGE("[Utils] __remill_write_memory_16 stack: [0x%lx] = 0x%lx", addr, val);
        const auto offset = addr - PREBUILT_STACK_BASE;
        *(uint16_t*)(Stack + offset) = val;
        return memory;
    }
    LOG_MESSAGE("[Utils] __remill_write_memory_16 write out of stack");
    exit(1);
    return memory;
}

void* __remill_write_memory_8(void *memory, addr_t addr, uint8_t val) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        LOG_MESSAGE("[Utils] __remill_write_memory_8 stack: [0x%lx] = 0x%lx", addr, val);
        const auto offset = addr - PREBUILT_STACK_BASE;
        *(uint8_t*)(Stack + offset) = val;
        return memory;
    }
    LOG_MESSAGE("[Utils] __remill_write_memory_8 write out of stack");
    exit(1);
    return memory;
}

*/

typedef struct {
    uint64_t addr;
    uint8_t val[PREBUILT_MEMORY_CELL_SIZE];
} MemoryCell64;

extern MemoryCell64 GlobalMemoryCells64[] = {
    {0x1234567890, {0}},
};

// For backward compatibility
uint64_t ReadGlobalMemory64EdgeChecked(void *memory, addr_t addr, size_t size) {
    return ReadGlobalMemoryEdgeChecked<uint64_t>(memory, addr);
}

/*
uint64_t __remill_read_memory_64(void *memory, addr_t addr) {

    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        const auto offset = addr - PREBUILT_STACK_BASE;
        const uint64_t val = *(uint64_t*)(Stack + offset);
        LOG_MESSAGE("[Utils] __remill_read_memory_64 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }

    return ReadGlobalMemoryEdgeChecked<uint64_t>(memory, addr);
}

// __remill_read_memory_32
uint32_t __remill_read_memory_32(void *memory, addr_t addr) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        const auto offset = addr - PREBUILT_STACK_BASE;
        const uint32_t val = *(uint32_t*)(Stack + offset);
        LOG_MESSAGE("[Utils] __remill_read_memory_32 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }

    return ReadGlobalMemoryEdgeChecked<uint32_t>(memory, addr);
}

// __remill_read_memory_16
uint16_t __remill_read_memory_16(void *memory, addr_t addr) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        const auto offset = addr - PREBUILT_STACK_BASE;
        const uint16_t val = *(uint16_t*)(Stack + offset);
        LOG_MESSAGE("[Utils] __remill_read_memory_16 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }
    return ReadGlobalMemoryEdgeChecked<uint16_t>(memory, addr);
}

// __remill_read_memory_8
uint8_t __remill_read_memory_8(void *memory, addr_t addr) {
    if (addr >= PREBUILT_STACK_BASE && addr < PREBUILT_STACK_BASE + PREBUILT_STACK_SIZE) {
        const auto offset = addr - PREBUILT_STACK_BASE;
        const uint8_t val = *(uint8_t*)(Stack + offset);
        LOG_MESSAGE("[Utils] __remill_read_memory_8 stack: 0x%lx = 0x%lx", addr, val);
        return val;
    }
    return ReadGlobalMemoryEdgeChecked<uint8_t>(memory, addr);
}
*/

// __remill_flag_computation_carry
bool __remill_flag_computation_carry(bool result, ...) {
    LOG_MESSAGE("[Utils] __remill_flag_computation_carry: %d", result);
    return result;
}

// __remill_flag_computation_zero
bool __remill_flag_computation_zero(bool result, ...) {
    LOG_MESSAGE("[Utils] __remill_flag_computation_zero: %d", result);
    return result;
}

// __remill_flag_computation_sign
bool __remill_flag_computation_sign(bool result, ...) {
    LOG_MESSAGE("[Utils] __remill_flag_computation_sign: %d", result);
    return result;
}

// __remill_flag_computation_overflow
bool __remill_flag_computation_overflow(bool result, ...) {
    LOG_MESSAGE("[Utils] __remill_flag_computation_overflow: %d", result);
    return result;
}

void* __remill_jump(void *state, addr_t addr, void* memory) {
    LOG_MESSAGE("[Utils] __remill_jump: 0x%lx", addr);
    return __remill_missing_block(state, addr, memory);
}

void* __remill_function_return(void *state, addr_t addr, void* memory) {
    LOG_MESSAGE("[Utils] __remill_function_return: 0x%lx", addr);
    return __remill_missing_block(state, addr, memory);
}

uint8_t __remill_undefined_8(void) {
    LOG_MESSAGE("[Utils] __remill_undefined_8");
    return 0;
}

uint16_t __remill_undefined_16(void) {
    LOG_MESSAGE("[Utils] __remill_undefined_16");
    return 0;
}

uint32_t __remill_undefined_32(void) {  
    LOG_MESSAGE("[Utils] __remill_undefined_32");
    return 0;
}

uint64_t __remill_undefined_64(void) {
    LOG_MESSAGE("[Utils] __remill_undefined_64");
    return 0;
}

bool __remill_compare_neq(bool result) {
    LOG_MESSAGE("[Utils] __remill_compare_neq: %d", result);
    return result;
}

} // extern "C"

