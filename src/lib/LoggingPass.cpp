#include "LoggingPass.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

llvm::PreservedAnalyses FunctionLoggingPass::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    // Declare the logging function if it doesn't exist
    llvm::FunctionType *LogFuncType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(M.getContext()),
        {llvm::Type::getInt8PtrTy(M.getContext())},
        false
    );
    
    llvm::Function *LogFunc = llvm::cast<llvm::Function>(
        M.getOrInsertFunction("__remill_log_function", LogFuncType).getCallee()
    );

    // Add logging to each function in the module
    for (auto &F : M) {
        if (!F.isDeclaration()) {
            insertLogging(F, LogFunc);
        }
    }

    return llvm::PreservedAnalyses::none();
}

void FunctionLoggingPass::insertLogging(llvm::Function &F, llvm::Function *LogFunc) {
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