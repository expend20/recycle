#include "AddMissingBlockHandler.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include <glog/logging.h>

#include <set>

namespace BitcodeManipulation {

void AddMissingBlockHandler(llvm::Module& M, 
    const std::vector<std::pair<uint64_t, std::string>>& addr_to_func) {
    
    auto& context = M.getContext();
    
    // Get or create the function type for missing block handler
    auto voidPtrTy = llvm::Type::getInt8PtrTy(context);
    auto int64Ty = llvm::Type::getInt64Ty(context);
    
    std::vector<llvm::Type*> argTypes = {voidPtrTy, int64Ty, voidPtrTy};
    auto funcTy = llvm::FunctionType::get(voidPtrTy, argTypes, false);
    
    // Get or create the missing block handler function
    auto missingBlockFunc = M.getOrInsertFunction("__remill_missing_block", funcTy);
    auto func = llvm::cast<llvm::Function>(missingBlockFunc.getCallee());

    // If the function already exists and has a body, we need to preserve existing cases
    std::set<uint64_t> existingAddresses;
    llvm::SwitchInst* existingSwitch = nullptr;
    llvm::BasicBlock* defaultBB = nullptr;

    if (!func->empty()) {
        // Find the existing switch instruction and collect existing cases
        for (auto& BB : *func) {
            for (auto& I : BB) {
                if (auto* switchInst = llvm::dyn_cast<llvm::SwitchInst>(&I)) {
                    existingSwitch = switchInst;
                    defaultBB = switchInst->getDefaultDest();
                    
                    // Collect existing addresses
                    for (auto Case : switchInst->cases()) {
                        if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(Case.getCaseValue())) {
                            existingAddresses.insert(constInt->getZExtValue());
                        }
                    }
                    break;
                }
            }
            if (existingSwitch) break;
        }
    }

    // If no existing function body, create new entry block and switch
    if (func->empty()) {
        auto entryBB = llvm::BasicBlock::Create(context, "entry", func);
        llvm::IRBuilder<> builder(entryBB);
        
        // Get function arguments
        auto args = func->arg_begin();
        auto state = args++;
        auto pc = args++;
        auto memory = args;
        
        // Create default block and switch instruction
        defaultBB = llvm::BasicBlock::Create(context, "default", func);
        auto switchInst = builder.CreateSwitch(pc, defaultBB);
        existingSwitch = switchInst;

        // Create the default block with call to __rt_missing_block
        llvm::IRBuilder<> defaultBuilder(defaultBB);
        auto finalFunc = M.getOrInsertFunction("__rt_missing_block", funcTy);
        auto call = defaultBuilder.CreateCall(finalFunc, {state, pc, memory});
        defaultBuilder.CreateRet(call);
    }

    // Add new cases for addresses that don't exist yet
    for (const auto& mapping : addr_to_func) {
        // Skip if this address already has a case
        if (existingAddresses.count(mapping.first) > 0) {
            VLOG(1) << "Skipping existing case for address 0x" << std::hex << mapping.first;
            continue;
        }

        auto caseBB = llvm::BasicBlock::Create(context, "case_" + std::to_string(mapping.first), func);
        llvm::IRBuilder<> builder(caseBB);
        
        // Get function arguments for the new case
        auto args = func->arg_begin();
        auto state = args++;
        auto pc = args++;
        auto memory = args;
        
        // Get or declare the target function
        auto targetFunc = M.getOrInsertFunction(mapping.second, funcTy);
        
        // Call the target function with the same arguments
        auto call = builder.CreateCall(targetFunc, {state, pc, memory});
        builder.CreateRet(call);
        
        // Add this case to the switch
        existingSwitch->addCase(llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), mapping.first), caseBB);
        VLOG(1) << "Added new case for address 0x" << std::hex << mapping.first;
    }
    
    // Verify the function
    std::string err;
    llvm::raw_string_ostream errStream(err);
    if (llvm::verifyFunction(*func, &errStream)) {
        LOG(ERROR) << "Failed to verify __remill_missing_block: " << err;
        return;
    }
    
    VLOG(1) << "Successfully updated missing block handler with " << addr_to_func.size() << " new mappings";
}

} // namespace BitcodeManipulation