#include "MiscUtils.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <glog/logging.h>
#include <set>

namespace MiscUtils {

// just clone the module
std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& M) {
    return llvm::CloneModule(M);
}

// just as a sample
void MergeModules(llvm::Module& M1, const llvm::Module& M2) {

    VLOG(1) << "Merging modules";

    VLOG(1) << "Verifying M2";
    if (llvm::verifyModule(M2, &llvm::errs())) {
        LOG(ERROR) << "M2 is not valid";
        return;
    }
    VLOG(1) << "Verifying M1";    
    if (llvm::verifyModule(M1, &llvm::errs())) {
        LOG(ERROR) << "M1 is not valid";
        return;
    }
    // Clone M2 to avoid modifying the original
    auto ClonedM2 = llvm::CloneModule(M2);
    VLOG(1) << "Cloned M2";
    
    // Link the modules
    llvm::Linker::linkModules(M1, std::move(ClonedM2));
    VLOG(1) << "Linked modules";
}

void DumpModule(const llvm::Module& M, const std::string& filename) {
    std::error_code EC;
    llvm::raw_fd_ostream file(filename, EC);
    if (EC) {
        LOG(ERROR) << "Could not open file: " << EC.message();
        return;
    }
    M.print(file, nullptr);
    file.close();
    LOG(INFO) << "LLVM IR written to " << filename;
}

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

        // Create the default block with call to __remill_missing_block_final
        llvm::IRBuilder<> defaultBuilder(defaultBB);
        auto finalFunc = M.getOrInsertFunction("__remill_missing_block_final", funcTy);
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

} // namespace MiscUtils 