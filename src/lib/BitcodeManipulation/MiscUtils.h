#pragma once

#include <string>
#include <vector>
#include <llvm/IR/Module.h>

namespace BitcodeManipulation {

std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& M);
void MergeModules(llvm::Module& M1, const llvm::Module& M2);
void DumpModule(const llvm::Module& M, const std::string& filename);

// TODO: move to standalone function
llvm::Function* CreateEntryWithState(llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName);

} // namespace BitcodeManipulation 