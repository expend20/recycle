#include "LoggingPass.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <glog/logging.h>

llvm::PreservedAnalyses FunctionLoggingPass::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    // First check if logging function already exists
    llvm::Function *LogFunc = M.getFunction("__remill_log_function");
    
    if (!LogFunc) {
        // Declare the logging function if it doesn't exist
        llvm::FunctionType *LogFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(M.getContext()),
            {llvm::Type::getInt8PtrTy(M.getContext())},
            false
        );
        
        LogFunc = llvm::Function::Create(
            LogFuncType,
            llvm::Function::ExternalLinkage,
            "__remill_log_function",
            &M
        );

        if (!LogFunc) {
            LOG(ERROR) << "Failed to create __remill_log_function";
            return llvm::PreservedAnalyses::none();
        }
    }

    // Add logging to each function in the module
    for (auto &F : M) {
        if (!F.isDeclaration()) {
            insertLogging(F, LogFunc);
        }
    }

    return llvm::PreservedAnalyses::none();
}

void FunctionLoggingPass::insertLogging(llvm::Function &F, llvm::Function *LogFunc) {
    // Check if function already has a logging call
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *Call = llvm::dyn_cast<llvm::CallInst>(&I)) {
                if (Call->getCalledFunction() == LogFunc) {
                    // Function already has logging, skip it
                    return;
                }
            }
        }
    }

    llvm::IRBuilder<> Builder(&*F.getEntryBlock().getFirstInsertionPt());
    
    // Create a global string with the function name
    llvm::Constant *StrConstant = llvm::ConstantDataArray::getString(
        F.getContext(), 
        F.getName()
    );
    
    llvm::GlobalVariable *GV = new llvm::GlobalVariable(
        *F.getParent(),
        StrConstant->getType(),
        true,
        llvm::GlobalValue::PrivateLinkage,
        StrConstant,
        ".str"
    );

    // Get pointer to the string
    llvm::Value *StrPtr = Builder.CreateBitCast(
        GV,
        llvm::Type::getInt8PtrTy(F.getContext())
    );

    // Insert the call to logging function
    Builder.CreateCall(LogFunc, {StrPtr});
} 