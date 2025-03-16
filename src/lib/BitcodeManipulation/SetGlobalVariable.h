#pragma once

#include <llvm/IR/Module.h>
#include <string>

namespace BitcodeManipulation {

void SetGlobalVariableUint64(
    llvm::Module& DestModule, 
    const std::string& VariableName,
    uint64_t Value);

} // namespace BitcodeManipulation