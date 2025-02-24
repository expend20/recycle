#pragma once

#include <llvm/IR/Module.h>

namespace BitcodeManipulation {

void InsertFunctionLogging(llvm::Module &M);

}  // namespace BitcodeManipulation
