#pragma once

#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <memory>

namespace llvm {
class Module;
}

class PassManagerWrapper {
public:
    PassManagerWrapper();

    // Apply the logging pass to the given module
    void ApplyLoggingPass(llvm::Module* module);

    // Apply the function rename pass to the given module
    void ApplyRenamePass(llvm::Module* module);

    // Apply the remove suffix pass to the given module
    void ApplyRemoveSuffixPass(llvm::Module* module);

private:
    // Analysis managers
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;
    
    // Pass builder for registering analyses
    llvm::PassBuilder PB;

    // Initialize the pass infrastructure
    void InitializePassInfrastructure();
}; 