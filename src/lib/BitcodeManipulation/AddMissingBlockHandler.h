#pragma once

#include <llvm/IR/Module.h>
#include <vector>
#include <string>

namespace BitcodeManipulation {
    void AddMissingBlockHandler(llvm::Module& M, 
        const std::vector<std::pair<uint64_t, std::string>>& addr_to_func);
}