#pragma once

#include <llvm/IR/PassManager.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>

class FunctionRemoveSuffixPass : public llvm::PassInfoMixin<FunctionRemoveSuffixPass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

private:
    void processCallInstructions(llvm::Module &M);
    bool tryRemoveSuffix(llvm::Module &M, llvm::Function *FunctionWithSuffix);
}; 