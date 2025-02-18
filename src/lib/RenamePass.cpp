/**
 * RenamePass.cpp
 * 
 * This pass handles function name mismatches that can occur during lifting and linking.
 * Specifically, it addresses a common issue where a function might be defined with a ".1" suffix
 * but called without it. This happens because:
 * 1. During lifting, functions might be given a ".1" suffix to avoid naming conflicts
 * 2. However, call instructions in the lifted code still use the original name without the suffix
 * 
 * For example:
 * - A function might be defined as "sub_1400016d0.1"
 * - But called as "sub_1400016d0"
 * 
 * This pass:
 * 1. Scans all call instructions in the module
 * 2. For each called function that doesn't exist in the module
 * 3. Checks if there's a version with ".1" suffix
 * 4. If found, renames the ".1" version to the called name
 * 
 * This ensures that all function calls can be properly resolved during linking and execution.
 */

#include "RenamePass.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/raw_ostream.h>
#include <glog/logging.h>

llvm::PreservedAnalyses FunctionRenamePass::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    processCallInstructions(M);
    return llvm::PreservedAnalyses::none();
}

void FunctionRenamePass::processCallInstructions(llvm::Module &M) {
    // First collect all call instructions to avoid modifying while iterating
    std::vector<llvm::StringRef> calledNames;
    
    // Iterate through all functions in the module
    for (auto &F : M) {
        if (F.isDeclaration()) {
            continue;  // Skip function declarations, we only care about definitions
        }
        
        // Scan each basic block in the function
        for (auto &BB : F) {
            for (auto &I : BB) {
                // Look for call instructions
                if (auto *CallInst = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    if (auto Called = CallInst->getCalledFunction()) {
                        // Direct function call
                        VLOG(1) << "Found direct call to " << Called->getName().str();
                        calledNames.push_back(Called->getName());
                    } else if (auto CalledValue = CallInst->getCalledOperand()) {
                        // Handle indirect calls where the function is loaded from a pointer
                        // This can happen when the function is referenced through a global variable
                        if (auto LoadInst = llvm::dyn_cast<llvm::LoadInst>(CalledValue)) {
                            if (auto GV = llvm::dyn_cast<llvm::GlobalVariable>(LoadInst->getPointerOperand())) {
                                calledNames.push_back(GV->getName());
                            }
                        }
                    }
                }
            }
        }
    }

    // Process collected names
    for (auto CalledName : calledNames) {
        tryRenameFunction(M, CalledName);
    }
}

bool FunctionRenamePass::tryRenameFunction(llvm::Module &M, llvm::StringRef CalledName) {
    // Check if the called function exists
    VLOG(1) << "Checking if " << CalledName.str() << " exists";
    auto *CalledF = M.getFunction(CalledName);
    // check if the function is a declaration
    if (CalledF) {
        if (!CalledF->isDeclaration()) {
            VLOG(1) << "Function " << CalledName.str() << " is not a declaration, no need to rename";
            return false;  // Function exists, no need to rename
        }
    }

    // Try to find a function with ".1" suffix
    auto NameWithSuffix = CalledName.str() + ".1";
    auto *FunctionToRename = M.getFunction(NameWithSuffix);
    if (!FunctionToRename) {
        return false;  // No function with ".1" suffix found
    }

    // Update callers if the original function exists
    if (CalledF) {
        for (const auto &U : CalledF->users()) {
            if (auto *CallInst = llvm::dyn_cast<llvm::CallInst>(U)) {
                VLOG(1) << "Updating caller to " << FunctionToRename->getName().str();
                CallInst->setCalledFunction(FunctionToRename);
            }
        }
        CalledF->eraseFromParent();
    }

    return true;
} 