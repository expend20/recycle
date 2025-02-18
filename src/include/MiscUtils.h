#pragma once

#include <string>
#include <llvm/IR/Module.h>

namespace MiscUtils {

std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& M);
void MergeModules(llvm::Module& M1, const llvm::Module& M2);
void DumpModule(const llvm::Module& M, const std::string& filename);

} // namespace MiscUtils 