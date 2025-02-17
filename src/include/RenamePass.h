#pragma once

#include <llvm/IR/PassManager.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>

class FunctionRenamePass : public llvm::PassInfoMixin<FunctionRenamePass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

private:
    void processCallInstructions(llvm::Module &M);
    bool tryRenameFunction(llvm::Module &M, llvm::StringRef CalledName);
}; 