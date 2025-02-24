#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {

/**
 * Removes ".1" suffixes from function names and redirects calls accordingly.
 * If a function without the ".1" suffix exists, redirects calls to that function
 * and removes the function with ".1" suffix from the module.
 */
void RemoveSuffixFromFunctions(llvm::Module &M);

}  // namespace BitcodeManipulation 