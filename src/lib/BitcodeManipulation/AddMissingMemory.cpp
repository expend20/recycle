#include "AddMissingMemory.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <glog/logging.h>

namespace BitcodeManipulation {

bool AddMissingMemory(llvm::Module &M, uint64_t addr, const std::vector<uint8_t>& page) {
    if (page.size() != PREBUILT_MEMORY_CELL_SIZE) {
        LOG(ERROR) << "Page size is not " << PREBUILT_MEMORY_CELL_SIZE;
        return false;
    }

    // Find the GlobalMemoryCells64 global variable
    auto* globalCells = M.getGlobalVariable("GlobalMemoryCells64");
    if (!globalCells) {
        LOG(ERROR) << "Could not find GlobalMemoryCells64 global variable";
        return false;
    }

    auto& context = M.getContext();

    // Create array for the page data
    std::vector<llvm::Constant*> pageValues;
    for (size_t i = 0; i < page.size() && i < PREBUILT_MEMORY_CELL_SIZE; i++) {
        pageValues.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), page[i]));
    }
    
    // Pad with zeros if needed
    while (pageValues.size() < PREBUILT_MEMORY_CELL_SIZE) {
        pageValues.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 0));
    }

    // Create the page data array
    auto* pageArray = llvm::ConstantArray::get(
        llvm::ArrayType::get(llvm::Type::getInt8Ty(context), PREBUILT_MEMORY_CELL_SIZE),
        pageValues
    );

    // Get the struct type from the global variable's type
    auto* arrayType = llvm::cast<llvm::ArrayType>(globalCells->getValueType());
    auto* structTy = llvm::cast<llvm::StructType>(arrayType->getElementType());

    // Create the new cell struct elements
    std::vector<llvm::Constant*> cellElements = {
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), addr), // addr
        pageArray // val array
    };
    
    // Create the new cell
    auto* newCell = llvm::ConstantStruct::get(structTy, cellElements);

    // Create new array with just our new element
    std::vector<llvm::Constant*> newElements;
    
    // If there's an existing initializer that's not zeroinitializer, add its elements
    if (auto* currentInit = llvm::dyn_cast<llvm::ConstantArray>(globalCells->getInitializer())) {
        for (unsigned i = 0; i < currentInit->getNumOperands(); i++) {
            newElements.push_back(currentInit->getOperand(i));
        }
    }
    
    // Add our new cell
    newElements.push_back(newCell);

    // First create a new global variable with the right size
    auto* newGlobal = new llvm::GlobalVariable(
        M,
        llvm::ArrayType::get(structTy, newElements.size()),
        false, // isConstant
        llvm::GlobalValue::ExternalLinkage,
        nullptr,
        "GlobalMemoryCells64_new"
    );

    // Create new array type and initializer
    auto* newArray = llvm::ConstantArray::get(
        llvm::ArrayType::get(structTy, newElements.size()),
        newElements
    );

    // Set the initializer on the new global
    newGlobal->setInitializer(newArray);

    // Replace all uses of the old global with the new one
    globalCells->replaceAllUsesWith(newGlobal);
    
    // Copy the name from the old global to the new one
    newGlobal->takeName(globalCells);
    
    // Remove the old global
    globalCells->eraseFromParent();

    LOG(INFO) << "Added memory cell for address 0x" << std::hex << addr 
              << " with " << std::dec << page.size() << " bytes";
              
    return true;
}
} 