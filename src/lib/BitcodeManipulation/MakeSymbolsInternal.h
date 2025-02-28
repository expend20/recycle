#pragma once

#include <llvm/IR/Module.h>
#include <vector>
#include <string>

namespace BitcodeManipulation {
    void MakeSymbolsInternal(llvm::Module& M, 
        const std::vector<std::string>& exceptions);
    
    void MakeFunctionsInline(llvm::Module& M, 
        const std::vector<std::string>& exceptions);
        
    void RemoveOptNoneAttribute(llvm::Module& M, 
        const std::vector<std::string>& exceptions);
} 