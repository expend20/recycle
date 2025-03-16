#pragma once

#include <string>
#include <vector>
#include <llvm/IR/Module.h>

namespace BitcodeManipulation {

void MergeModules(llvm::Module& M1, const llvm::Module& M2);
void DumpModule(const llvm::Module& M, const std::string& filename);
std::unique_ptr<llvm::Module> ReadBitcodeFile(const std::string& filename, llvm::LLVMContext& Context);

} // namespace BitcodeManipulation 