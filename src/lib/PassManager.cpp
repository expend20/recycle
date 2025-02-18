#include "PassManager.h"
#include "LoggingPass.h"
#include "RenamePass.h"
#include "RemoveSuffixPass.h"
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>

PassManagerWrapper::PassManagerWrapper() {
    InitializePassInfrastructure();
}

void PassManagerWrapper::InitializePassInfrastructure() {
    // Register all the basic analyses with the managers
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
}

void PassManagerWrapper::ApplyLoggingPass(llvm::Module* module) {
    // Create and run the logging pass
    llvm::ModulePassManager MPM;
    MPM.addPass(FunctionLoggingPass());
    MPM.run(*module, MAM);
}

void PassManagerWrapper::ApplyRenamePass(llvm::Module* module) {
    // Create and run the rename pass
    llvm::ModulePassManager MPM;
    MPM.addPass(FunctionRenamePass());
    MPM.run(*module, MAM);
}

void PassManagerWrapper::ApplyRemoveSuffixPass(llvm::Module* module) {
    // Create and run the remove suffix pass
    llvm::ModulePassManager MPM;
    MPM.addPass(FunctionRemoveSuffixPass());
    MPM.run(*module, MAM);
} 