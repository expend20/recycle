#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {
    // Replaces calls to __rt_missing_block with direct calls to specific functions
    // if a matching function with name "sub_<hex address>" exists in the module.
    // Returns the number of calls that were replaced.
    uint64_t ReplaceMissingBlockCalls(llvm::Module &M,
                                      const std::string &missingBlockFuncName = "__rt_missing_block");
} 