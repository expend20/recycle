#pragma once

#include <llvm/IR/PassManager.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

class FunctionLoggingPass : public llvm::PassInfoMixin<FunctionLoggingPass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

private:
    void insertLogging(llvm::Function &F, llvm::Function *LogFunc);
}; 