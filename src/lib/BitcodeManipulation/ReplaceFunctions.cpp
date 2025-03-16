#include "BitcodeManipulation/BitcodeManipulation.h"
#include "BitcodeManipulation/ReplaceFunctions.h"

#include <glog/logging.h>

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/DebugInfo.h>
#include <memory>

namespace BitcodeManipulation {

llvm::Function* ReplaceFunction(
    llvm::Module& DestModule, 
    const std::string& OldFunctionName,
    const std::string& NewFunctionName)
{
    VLOG(1) << "Replacing function '" << OldFunctionName << "' with '" << NewFunctionName << "'";
    // Find the function to be replaced in the destination module
    llvm::Function* OldFunction = DestModule.getFunction(OldFunctionName);
    if (!OldFunction) {
        throw std::runtime_error("Could not find function '" + OldFunctionName + "' in the destination module");
    }

    // Find the new function in the destination module
    llvm::Function* NewFunction = DestModule.getFunction(NewFunctionName);
    if (!NewFunction) {
        throw std::runtime_error("Could not find function '" + NewFunctionName + "' in the destination module");
    }

    // Verify that the functions have compatible signatures
    if (OldFunction->getFunctionType() != NewFunction->getFunctionType()) {
        throw std::runtime_error("Function types of '" + OldFunctionName + "' and '" + NewFunctionName + "' are not compatible");
    }

    // Replace all uses of the old function with the new function
    OldFunction->replaceAllUsesWith(NewFunction);
    
    // If the old function is not used anymore, we can remove it
    if (OldFunction->use_empty()) {
        OldFunction->eraseFromParent();
    }

    return NewFunction;
}

} // namespace BitcodeManipulation 