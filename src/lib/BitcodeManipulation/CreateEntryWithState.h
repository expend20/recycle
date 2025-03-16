#pragma once

#include <llvm/IR/Module.h>
#include <string>

namespace BitcodeManipulation {

llvm::Function* CreateEntryFunction(
    llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName);

llvm::Function* CreateManualEntryStructs(
    llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName);
}