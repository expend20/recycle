#pragma once

#include <llvm/IR/Module.h>
#include <vector>
#include <cstdint>

namespace BitcodeManipulation {
    // Extract all destination addresses from calls to __rt_missing_block function
    // Returns a vector of unique addresses (as uint64_t)
    std::vector<uint64_t> ExtractMissingBlocks(llvm::Module& M, const std::string& function_name = "__rt_missing_block");
    
    // Utility function to print the extracted missing blocks in a readable format
    void PrintMissingBlocks(const std::vector<uint64_t>& blocks);
} 