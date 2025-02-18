/**
 * RemoveSuffixPass.cpp
 * 
 * This pass handles function name mismatches by removing ".1" suffixes from function names.
 * It does the reverse of RenamePass by:
 * 1. Finding all call instructions that call functions with ".1" suffix
 * 2. If a function without the ".1" suffix exists, redirects the call to that function
 * 3. Removes the function with ".1" suffix from the module
 * 
 * For example:
 * - If there's a call to "sub_1400016d0.1"
 * - And "sub_1400016d0" exists
 * - The call will be redirected to "sub_1400016d0"
 * - And "sub_1400016d0.1" will be removed
 */

#include "RemoveSuffixPass.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/raw_ostream.h>
#include <set>

llvm::PreservedAnalyses FunctionRemoveSuffixPass::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    processCallInstructions(M);
    return llvm::PreservedAnalyses::none();
}

void FunctionRemoveSuffixPass::processCallInstructions(llvm::Module &M) {
    // First collect all functions with ".1" suffix to avoid modifying while iterating
    std::vector<llvm::Function*> functionsToProcess;
    
    // Iterate through all functions in the module
    for (auto &F : M) {
        if (F.isDeclaration()) {
            continue;  // Skip function declarations
        }
        
        llvm::StringRef Name = F.getName();
        if (Name.endswith(".1")) {
            //llvm::outs() << "Found function with .1 suffix: " << Name << "\n";
            functionsToProcess.push_back(&F);
        }
    }

    // Process collected functions
    for (auto *F : functionsToProcess) {
        tryRemoveSuffix(M, F);
    }
}

bool FunctionRemoveSuffixPass::tryRemoveSuffix(llvm::Module &M, llvm::Function *FunctionWithSuffix) {
    llvm::StringRef NameWithSuffix = FunctionWithSuffix->getName();
    
    // Get the name without ".1" suffix
    std::string NameWithoutSuffix = NameWithSuffix.substr(0, NameWithSuffix.size() - 2).str();
    //llvm::outs() << "Trying to remove suffix from " << NameWithSuffix << " to " << NameWithoutSuffix << "\n";
    // Check if the function without suffix exists
    auto *TargetFunction = M.getFunction(NameWithoutSuffix);
    if (!TargetFunction) {
        //llvm::outs() << "Function without suffix doesn't exist\n";
        return false;  // Function without suffix doesn't exist
    }

    // Collect all global variables used by this function
    std::set<llvm::GlobalVariable*> usedGlobals;
    for (auto &BB : *FunctionWithSuffix) {
        for (auto &I : BB) {
            // Check for global variable uses in instructions
            for (auto &Op : I.operands()) {
                if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(Op)) {
                    usedGlobals.insert(GV);
                }
            }
        }
    }

    // Update all callers to use the function without suffix
    bool HasUsers = false;
    for (const auto &U : FunctionWithSuffix->users()) {
        //llvm::outs() << "User of " << NameWithSuffix << ": " << U->getName() << "\n";
        if (auto *CallInst = llvm::dyn_cast<llvm::CallInst>(U)) {
            HasUsers = true;
            llvm::outs() << "Updating caller of " << NameWithSuffix << " to " << NameWithoutSuffix << "\n";
            CallInst->setCalledFunction(TargetFunction);
        }
    }

    // If the function with suffix was used and all uses were updated
    if (HasUsers) {
        // Check each global variable we found
        for (auto *GV : usedGlobals) {
            bool isExclusiveToThisFunction = true;
            
            // Check if this global is used by any other function
            for (const auto &U : GV->users()) {
                // Get the parent function of this user
                llvm::Function *UserFunction = nullptr;
                if (auto *I = llvm::dyn_cast<llvm::Instruction>(U)) {
                    UserFunction = I->getParent()->getParent();
                }
                
                // If used by another function (not the one we're removing), mark as non-exclusive
                if (UserFunction && UserFunction != FunctionWithSuffix) {
                    isExclusiveToThisFunction = false;
                    break;
                }
            }
            
            // If global is exclusively used by this function, remove it
            if (isExclusiveToThisFunction) {
                llvm::outs() << "Removing global variable '" << GV->getName() << "' exclusively used by " << NameWithSuffix << "\n";
                
                // First remove all uses of this global
                while (!GV->use_empty()) {
                    llvm::User *U = *GV->user_begin();
                    if (auto *Call = llvm::dyn_cast<llvm::CallInst>(U)) {
                        // If it's a call instruction, remove it
                        Call->eraseFromParent();
                    } else {
                        // For other uses, try to replace with undef
                        U->replaceUsesOfWith(GV, llvm::UndefValue::get(GV->getType()));
                    }
                }
                
                // Now safe to remove the global
                GV->eraseFromParent();
            }
        }

        FunctionWithSuffix->eraseFromParent();
        return true;
    }

    return false;
} 