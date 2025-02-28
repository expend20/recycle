#include "MakeSymbolsInternal.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Attributes.h>

#include <glog/logging.h>

#include <algorithm>
#include <unordered_set>

namespace BitcodeManipulation {

// Helper function to determine if a symbol is an LLVM intrinsic
static bool isLLVMIntrinsic(llvm::StringRef Name) {
    return Name.startswith("llvm.");
}

void RemoveOptNoneAttribute(llvm::Module& M, 
    const std::vector<std::string>& exceptions) {
    
    // Convert exceptions list to a set for O(1) lookup
    std::unordered_set<std::string> exceptionsSet(exceptions.begin(), exceptions.end());
    
    // Process functions
    for (auto& F : M) {
        // Skip LLVM intrinsics, exceptions, and function declarations
        if (isLLVMIntrinsic(F.getName()) || 
            exceptionsSet.count(F.getName().str()) || 
            F.isDeclaration()) {
            VLOG(1) << "Skipping function (intrinsic, declaration, or in exceptions list): " 
                   << F.getName().str();
            continue;
        }
        
        // Check if the function has the optnone attribute and remove it if present
        if (F.hasFnAttribute(llvm::Attribute::OptimizeNone)) {
            F.removeFnAttr(llvm::Attribute::OptimizeNone);
            VLOG(1) << "Removed OptimizeNone attribute from function: " << F.getName().str();
        }
    }
    
    VLOG(1) << "Successfully processed OptimizeNone attributes in module: " << M.getName().str();
}

void MakeFunctionsInline(llvm::Module& M, 
    const std::vector<std::string>& exceptions) {
    
    // Convert exceptions list to a set for O(1) lookup
    std::unordered_set<std::string> exceptionsSet(exceptions.begin(), exceptions.end());
    
    // Process functions
    for (auto& F : M) {
        // Skip LLVM intrinsics, exceptions, and function declarations
        if (isLLVMIntrinsic(F.getName()) || 
            exceptionsSet.count(F.getName().str()) || 
            F.isDeclaration()) {
            VLOG(1) << "Skipping function (intrinsic, declaration, or in exceptions list): " 
                   << F.getName().str();
            continue;
        }
        
        // Remove NoInline attribute if present
        if (F.hasFnAttribute(llvm::Attribute::NoInline)) {
            F.removeFnAttr(llvm::Attribute::NoInline);
            VLOG(1) << "Removed NoInline attribute from function: " << F.getName().str();
        }
        
        // Mark the function as inline
        F.addFnAttr(llvm::Attribute::AlwaysInline);
        VLOG(1) << "Set inline attribute for function: " << F.getName().str();
    }
    
    VLOG(1) << "Successfully marked eligible functions as inline in module: " << M.getName().str();
}

void MakeSymbolsInternal(llvm::Module& M, 
    const std::vector<std::string>& exceptions) {
    
    // Convert exceptions list to a set for O(1) lookup
    std::unordered_set<std::string> exceptionsSet(exceptions.begin(), exceptions.end());
    
    // Process global variables
    for (auto& GV : M.globals()) {
        // Skip LLVM intrinsics and exceptions
        if (isLLVMIntrinsic(GV.getName()) || exceptionsSet.count(GV.getName().str())) {
            VLOG(1) << "Skipping global variable (intrinsic or in exceptions list): " << GV.getName().str();
            continue;
        }
        
        // If the global is not in the exceptions list, make it internal
        if (!GV.hasLocalLinkage()) {
            GV.setLinkage(llvm::GlobalValue::InternalLinkage);
            VLOG(1) << "Set internal linkage for global variable: " << GV.getName().str();
        }
    }
    
    // Process functions
    for (auto& F : M) {
        // Skip LLVM intrinsics and exceptions
        if (isLLVMIntrinsic(F.getName()) || exceptionsSet.count(F.getName().str())) {
            VLOG(1) << "Skipping function (intrinsic or in exceptions list): " << F.getName().str();
            continue;
        }
        
        // If the function is not in the exceptions list, make it internal
        if (!F.hasLocalLinkage() && !F.isDeclaration()) {
            F.setLinkage(llvm::GlobalValue::InternalLinkage);
            VLOG(1) << "Set internal linkage for function: " << F.getName().str();
        }
    }
    
    VLOG(1) << "Successfully updated linkage for symbols in module: " << M.getName().str();
}

} // namespace BitcodeManipulation 