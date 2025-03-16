#include "SetGlobalVariable.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <glog/logging.h>

namespace BitcodeManipulation {

void SetGlobalVariableUint64(
    llvm::Module& DestModule, 
    const std::string& VariableName,
    uint64_t Value) {
    
    // Find the global variable in the module
    llvm::GlobalVariable* GV = DestModule.getNamedGlobal(VariableName);
    
    if (!GV) {
        LOG(ERROR) << "Global variable '" << VariableName << "' not found in the module";
        return;
    }
    
    // Get the context from the module
    llvm::LLVMContext& Context = DestModule.getContext();
    
    // Create a 64-bit integer constant with the provided value
    llvm::Constant* NewValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(Context), Value);
    
    // If the global variable's type doesn't match, try to create a compatible constant
    if (GV->getValueType() != llvm::Type::getInt64Ty(Context)) {
        // Check if we can create a constant of the required type
        if (llvm::Constant* CompatibleValue = llvm::ConstantExpr::getIntToPtr(
                NewValue, GV->getValueType())) {
            NewValue = CompatibleValue;
        } else {
            LOG(ERROR) << "Cannot convert uint64_t value to the type of global variable '" 
                      << VariableName << "'";
            return;
        }
    }
    
    // Set the initializer to the new value
    GV->setInitializer(NewValue);
    
    VLOG(1) << "Successfully set global variable '" << VariableName 
            << "' to value " << Value;
}

} // namespace BitcodeManipulation 