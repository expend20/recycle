#include "PassManager.h"
#include "LoggingPass.h"
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