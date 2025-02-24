#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {

// Creates a function in the module that can look up saved memory cells
// The function has signature: uintptr_t __rt_get_saved_memory_ptr(uintptr_t ptr)
// Returns a pointer to the memory if found, 0 otherwise
llvm::Function* CreateGetSavedMemoryPtr(llvm::Module &M);

} // namespace BitcodeManipulation 