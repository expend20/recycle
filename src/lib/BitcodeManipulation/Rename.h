#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {

/**
 * Handles function name mismatches that can occur during lifting and linking.
 * Specifically, it addresses a common issue where a function might be defined with a ".1" suffix
 * but called without it. This happens because:
 * 1. During lifting, functions might be given a ".1" suffix to avoid naming conflicts
 * 2. However, call instructions in the lifted code still use the original name without the suffix
 */
void RenameFunctions(llvm::Module &M);

void RenameFunction(llvm::Module &M, llvm::StringRef OldName, llvm::StringRef NewName);

}  // namespace BitcodeManipulation 