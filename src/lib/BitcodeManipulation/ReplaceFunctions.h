#pragma once

#include <llvm/IR/Module.h>
#include <string>

namespace BitcodeManipulation {

llvm::Function* ReplaceFunction(
    llvm::Module& DestModule, 
    const std::string& OldFunctionName,
    const std::string& NewFunctionName);

} // namespace BitcodeManipulation