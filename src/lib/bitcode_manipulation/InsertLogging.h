#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeTools {
void InsertFunctionLogging(llvm::Module &M);
}  // namespace BitcodeTools
