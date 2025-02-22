#include "Rename.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/raw_ostream.h>
#include <glog/logging.h>

namespace BitcodeTools {

namespace {
bool tryRenameFunction(llvm::Module &M, llvm::StringRef CalledName) {
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
} // anonymous namespace

void RenameFunctions(llvm::Module &M) {
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

}  // namespace BitcodeTools 