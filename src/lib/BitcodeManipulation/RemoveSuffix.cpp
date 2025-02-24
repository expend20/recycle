#include "RemoveSuffix.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <glog/logging.h>

namespace BitcodeManipulation {

namespace {
bool tryRemoveSuffix(llvm::Module &M, llvm::Function *FunctionWithSuffix) {
    llvm::StringRef NameWithSuffix = FunctionWithSuffix->getName();
    
    // Get the name without ".1" suffix
    std::string NameWithoutSuffix = NameWithSuffix.substr(0, NameWithSuffix.size() - 2).str();
    VLOG(1) << "Trying to remove suffix from " << NameWithSuffix.str() << " to " << NameWithoutSuffix;
    
    // Check if the function without suffix exists
    auto *TargetFunction = M.getFunction(NameWithoutSuffix);
    if (!TargetFunction) {
        VLOG(1) << "Function without suffix doesn't exist";
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
        if (auto *CallInst = llvm::dyn_cast<llvm::CallInst>(U)) {
            HasUsers = true;
            LOG(INFO) << "Updating caller of " << NameWithSuffix.str() << " to " << NameWithoutSuffix;
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
                LOG(INFO) << "Removing global variable '" << GV->getName().str() << "' exclusively used by " << NameWithSuffix.str();
                
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
} // anonymous namespace

void RemoveSuffixFromFunctions(llvm::Module &M) {
    // First collect all functions with ".1" suffix to avoid modifying while iterating
    std::vector<llvm::Function*> functionsToProcess;
    
    // Iterate through all functions in the module
    for (auto &F : M) {
        if (F.isDeclaration()) {
            continue;  // Skip function declarations
        }
        
        llvm::StringRef Name = F.getName();
        if (Name.endswith(".1")) {
            VLOG(1) << "Found function with .1 suffix: " << Name.str();
            functionsToProcess.push_back(&F);
        }
    }

    // Process collected functions
    for (auto *F : functionsToProcess) {
        tryRemoveSuffix(M, F);
    }
}

}  // namespace BitcodeManipulation 