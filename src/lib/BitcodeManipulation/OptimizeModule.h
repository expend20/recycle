#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {
    // Apply optimizations to the given module
    // level: 0=none, 1=O1, 2=O2, 3=O3
    void OptimizeModule(llvm::Module& M, unsigned level = 3);
} 