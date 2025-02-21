#include "InsertLogging.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <glog/logging.h>

namespace BitcodeTools {

void InsertFunctionLogging(llvm::Module &M) {
    // First check if logging function already exists
    llvm::Function *LogFunc = M.getFunction("LogMessage");
    
    if (!LogFunc) {
        // Declare the logging function if it doesn't exist
        llvm::FunctionType *LogFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(M.getContext()),
            {llvm::Type::getInt8PtrTy(M.getContext())},
            true  // isVarArg = true
        );
        
        LogFunc = llvm::Function::Create(
            LogFuncType,
            llvm::Function::ExternalLinkage,
            "LogMessage",
            &M
        );

        if (!LogFunc) {
            LOG(ERROR) << "Failed to create LogMessage";
            return;
        }
    }

    // Add logging to each function in the module
    for (auto &F : M) {
        if (F.isDeclaration()) {
            continue;
        }

        // Check if function already has a logging call
        bool hasLogging = false;
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *Call = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    if (Call->getCalledFunction() == LogFunc) {
                        hasLogging = true;
                        break;
                    }
                }
            }
            if (hasLogging) break;
        }
        if (hasLogging) continue;

        llvm::IRBuilder<> Builder(&*F.getEntryBlock().getFirstInsertionPt());
        
        // Create a global string with the format string
        llvm::Constant *FormatStr = llvm::ConstantDataArray::getString(
            F.getContext(), 
            "Entering function: %s at PC: 0x%lx"
        );
        
        llvm::GlobalVariable *FormatGV = new llvm::GlobalVariable(
            *F.getParent(),
            FormatStr->getType(),
            true,
            llvm::GlobalValue::PrivateLinkage,
            FormatStr,
            ".str.format"
        );

        // Create a global string with the function name
        llvm::Constant *NameStr = llvm::ConstantDataArray::getString(
            F.getContext(), 
            F.getName()
        );
        
        llvm::GlobalVariable *NameGV = new llvm::GlobalVariable(
            *F.getParent(),
            NameStr->getType(),
            true,
            llvm::GlobalValue::PrivateLinkage,
            NameStr,
            ".str.name"
        );

        // Get pointers to the strings
        llvm::Value *FormatPtr = Builder.CreateBitCast(
            FormatGV,
            llvm::Type::getInt8PtrTy(F.getContext())
        );

        llvm::Value *NamePtr = Builder.CreateBitCast(
            NameGV,
            llvm::Type::getInt8PtrTy(F.getContext())
        );

        // Get the program_counter argument
        llvm::Value *ProgramCounter = nullptr;
        for (auto &Arg : F.args()) {
            if (Arg.getArgNo() == 1) { // program_counter is the second argument
                ProgramCounter = &Arg;
                break;
            }
        }

        // Insert the call to logging function with format string, name, and program counter
        if (ProgramCounter) {
            Builder.CreateCall(LogFunc, {FormatPtr, NamePtr, ProgramCounter});
        } else {
            // If no program counter available, use 0 as default
            Builder.CreateCall(LogFunc, {FormatPtr, NamePtr, 
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(F.getContext()), 0)});
        }
    }
}

}  // namespace BitcodeTools
