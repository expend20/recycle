#include "AddMissingMemory.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <glog/logging.h>
#include "Prebuilt/Utils.h"

namespace BitcodeManipulation {

llvm::Function* CreateGetSavedMemoryPtr(llvm::Module &M) {
    auto& context = M.getContext();

    // Create function type: uintptr_t (uintptr_t)
    auto int64Ty = llvm::Type::getInt64Ty(context);
    auto funcTy = llvm::FunctionType::get(int64Ty, {int64Ty}, false);
    
    // Create the new function with a temporary name
    auto* newFunc = llvm::Function::Create(
        funcTy,
        llvm::GlobalValue::ExternalLinkage,
        "__rt_get_saved_memory_ptr_new",
        &M
    );
    
    // If there's an existing function, replace its uses and delete it
    if (auto* existingFunc = M.getFunction("__rt_get_saved_memory_ptr")) {
        // Replace all uses of the existing function with the new one
        existingFunc->replaceAllUsesWith(newFunc);
        existingFunc->eraseFromParent();
    }
    
    // Now rename the new function to the desired name
    newFunc->setName("__rt_get_saved_memory_ptr");

    // Create basic blocks
    auto entryBB = llvm::BasicBlock::Create(context, "entry", newFunc);
    auto loopBB = llvm::BasicBlock::Create(context, "loop", newFunc);
    auto checkBB = llvm::BasicBlock::Create(context, "check", newFunc);
    auto returnFoundBB = llvm::BasicBlock::Create(context, "return_found", newFunc);
    auto continueLoopBB = llvm::BasicBlock::Create(context, "continue_loop", newFunc);
    auto returnNotFoundBB = llvm::BasicBlock::Create(context, "return_not_found", newFunc);

    llvm::IRBuilder<> builder(entryBB);

    // Get function argument (ptr)
    auto ptr = newFunc->arg_begin();
    ptr->setName("ptr");

    // Find the GlobalMemoryCells64 global variable
    auto* globalCells = M.getGlobalVariable("GlobalMemoryCells64");
    if (!globalCells) {
        // If no global cells exist, always return 0
        builder.CreateRet(llvm::ConstantInt::get(int64Ty, 0));
        LOG(ERROR) << "GlobalMemoryCells64 not found";
        return nullptr;
    }

    // Get array size from global variable type
    auto* arrayType = llvm::cast<llvm::ArrayType>(globalCells->getValueType());
    auto numElements = arrayType->getNumElements();
    
    // Initialize loop counter
    auto counter = builder.CreateAlloca(int64Ty);
    builder.CreateStore(llvm::ConstantInt::get(int64Ty, 0), counter);
    builder.CreateBr(loopBB);

    // Loop block
    builder.SetInsertPoint(loopBB);
    auto currentIdx = builder.CreateLoad(int64Ty, counter);
    auto endCond = builder.CreateICmpULT(currentIdx, 
        llvm::ConstantInt::get(int64Ty, numElements));
    builder.CreateCondBr(endCond, checkBB, returnNotFoundBB);

    // Check block
    builder.SetInsertPoint(checkBB);
    
    // First get pointer to the current cell
    std::vector<llvm::Value*> indices = {
        llvm::ConstantInt::get(int64Ty, 0),  // Array index
        currentIdx                            // Cell index
    };
    auto cellPtr = builder.CreateGEP(arrayType, globalCells, indices);
    
    // Then get pointer to the address field within the cell
    indices = {
        llvm::ConstantInt::get(int64Ty, 0),  // Struct field (addr)
    };
    auto addrPtr = builder.CreateStructGEP(arrayType->getElementType(), cellPtr, 0);
    auto cellAddr = builder.CreateLoad(int64Ty, addrPtr);

    // Calculate the end address of the cell (cellAddr + PREBUILT_MEMORY_CELL_SIZE)
    auto cellEndAddr = builder.CreateAdd(cellAddr, 
        llvm::ConstantInt::get(int64Ty, PREBUILT_MEMORY_CELL_SIZE));
    
    // Check if ptr >= cellAddr
    auto isGreaterEqual = builder.CreateICmpUGE(ptr, cellAddr);
    // Check if ptr < cellEndAddr
    auto isLessThan = builder.CreateICmpULT(ptr, cellEndAddr);
    // Combine both conditions
    auto matches = builder.CreateAnd(isGreaterEqual, isLessThan);

    builder.CreateCondBr(matches, returnFoundBB, continueLoopBB);

    // Return found block
    builder.SetInsertPoint(returnFoundBB);
    // Get pointer to the data array field within the cell
    auto dataPtr = builder.CreateStructGEP(arrayType->getElementType(), cellPtr, 1);
    // Cast array pointer to integer
    auto result = builder.CreatePtrToInt(dataPtr, int64Ty);
    builder.CreateRet(result);

    // Continue loop block
    builder.SetInsertPoint(continueLoopBB);
    auto nextIdx = builder.CreateAdd(currentIdx, llvm::ConstantInt::get(int64Ty, 1));
    builder.CreateStore(nextIdx, counter);
    builder.CreateBr(loopBB);

    // Return not found block
    builder.SetInsertPoint(returnNotFoundBB);
    builder.CreateRet(llvm::ConstantInt::get(int64Ty, 0));

    return newFunc;
}

} // namespace BitcodeManipulation 