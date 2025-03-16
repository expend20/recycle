#include "ReplaceMissingBlockCalls.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include <glog/logging.h>
#include <sstream>
#include <iomanip>

namespace BitcodeManipulation {

uint64_t ReplaceMissingBlockCalls(llvm::Module& M, 
                                  const std::string& missingBlockFuncName) {
    VLOG(1) << "Replacing missing block calls in module: " << M.getName().str();
    
    // Get the function declaration for __rt_missing_block
    llvm::Function* missingBlockFunc = M.getFunction(missingBlockFuncName);
    
    // If the function doesn't exist in the module, return 0
    if (!missingBlockFunc) {
        VLOG(1) << "No " << missingBlockFuncName << " function found in module";
        return 0;
    }
    
    uint64_t replacedCalls = 0;
    std::vector<llvm::CallInst*> callsToReplace;
    
    // Iterate through all functions in the module
    for (auto& F : M) {
        // Skip the declaration of __rt_missing_block itself
        if (&F == missingBlockFunc) {
            continue;
        }

        
        // First, collect all calls to __rt_missing_block
        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    if (callInst->getCalledFunction() == missingBlockFunc) {
                        callsToReplace.push_back(callInst);
                    }
                }
            }
        }
    }

    VLOG(1) << "Found " << callsToReplace.size() << " calls to " << missingBlockFuncName;
    
    // Process collected calls for replacement
    for (auto* callInst : callsToReplace) {
        // The second argument (index 1) is the destination address
        if (callInst->arg_size() > 1) {
            // Try to extract the constant integer value
            if (auto* constArg = llvm::dyn_cast<llvm::ConstantInt>(callInst->getArgOperand(1))) {
                uint64_t destAddr = constArg->getZExtValue();
                
                // Create function name from address: sub_<hex address>
                std::stringstream ss;
                ss << "sub_" << std::hex << destAddr;
                std::string funcName = ss.str();
                
                VLOG(1) << "Replacing call to " << missingBlockFuncName << " with " << funcName << "...";
                // Look for a function with this name in the module
                if (llvm::Function* targetFunc = M.getFunction(funcName)) {
                    VLOG(1) << "Replacing call to " << missingBlockFuncName << " with " << funcName 
                             << " at address 0x" << std::hex << destAddr;
                    
                    // Create a new call instruction to the target function
                    llvm::IRBuilder<> builder(callInst);
                    std::vector<llvm::Value*> args;
                    
                    // Copy the arguments from the original call
                    for (unsigned i = 0; i < callInst->arg_size(); ++i) {
                        args.push_back(callInst->getArgOperand(i));
                    }
                    
                    // Create the new call
                    llvm::CallInst* newCall = builder.CreateCall(targetFunc, args, "");
                    
                    // Copy attributes from old call to new call
                    newCall->setCallingConv(callInst->getCallingConv());
                    newCall->setAttributes(callInst->getAttributes());
                    
                    // Replace uses of the old call with the new call
                    callInst->replaceAllUsesWith(newCall);
                    
                    // Remove the old call
                    callInst->eraseFromParent();
                    
                    replacedCalls++;
                } else {
                    VLOG(1) << "No function found for name " << funcName 
                             << " at address 0x" << std::hex << destAddr;
                }
            }
        }
    }
    
    LOG(INFO) << "Replaced " << replacedCalls << " calls to __rt_missing_block with direct function calls";
    
    return replacedCalls;
}

} // namespace BitcodeManipulation 