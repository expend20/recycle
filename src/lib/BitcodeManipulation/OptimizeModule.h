#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {
    // Apply optimizations to the given module
    // level: 0=none, 1=O1, 2=O2, 3=O3
    void OptimizeModule(llvm::Module& M, unsigned level = 3);
    
    // Inline functions in the module without applying other optimizations
    // This only performs function inlining and does not run any other optimization passes
    // If targetFunctionName is provided, only that function will be inlined
    void InlineFunctionsInModule(llvm::Module& M, const std::string& targetFunctionName = "");
} 