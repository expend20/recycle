#pragma once

#include <string>
#include <vector>
#include <llvm/IR/Module.h>

namespace MiscUtils {

std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& M);
void MergeModules(llvm::Module& M1, const llvm::Module& M2);
void DumpModule(const llvm::Module& M, const std::string& filename);

// Add missing block handler with address-to-function mappings
void AddMissingBlockHandler(llvm::Module& M, 
    const std::vector<std::pair<uint64_t, std::string>>& addr_to_func);

llvm::Function* CreateEntryWithState(llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName);

} // namespace MiscUtils 