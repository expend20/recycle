#include "BitcodeManipulation/BitcodeManipulation.h"

#include <glog/logging.h>

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LLVMContext.h>

namespace BitcodeManipulation {

llvm::Function* CreateManualEntryStructs(
    llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName)
{
    VLOG(1) << "Creating manual entry structs";
    auto& context = M.getContext();

    // create global variable Stack if not exists
    if (!M.getGlobalVariable("Stack")) {
        VLOG(1) << "Creating global variable Stack";
        auto stackTy = llvm::Type::getInt64Ty(context);
        auto stack = new llvm::GlobalVariable(M, stackTy, false, llvm::GlobalValue::ExternalLinkage, llvm::Constant::getNullValue(stackTy), "Stack");
        stack->setAlignment(llvm::Align(8));
        stack->setInitializer(llvm::Constant::getNullValue(stackTy));
    }

    // check if "main" function exists
    auto mainFunc = M.getFunction("main");
    if (!mainFunc)
    {
        VLOG(1) << "Creating main function";
        // create main function
        auto voidTy = llvm::Type::getVoidTy(context);
        auto funcTy = llvm::FunctionType::get(voidTy, false);
        mainFunc = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage, "main", &M);

        // create %struct.State = type { %struct.X86State } struct
        // first get type of %struct.X86State
        auto x86StateType = llvm::StructType::getTypeByName(context, "struct.X86State");
        if (!x86StateType) {
            LOG(ERROR) << "Failed to find struct.X86State type";
            return nullptr;
        }

        // create struct type for %struct.State
        auto stateType = llvm::StructType::create(context, "struct.State");
        stateType->setBody(x86StateType);

        // Create a basic block for the main function first
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", mainFunc);
        llvm::IRBuilder<> builder(entryBB);
        // No need to set insert point since the IRBuilder constructor already does that
        // builder.SetInsertPoint(entryBB, entryBB->getFirstNonPHI()->getIterator());
        auto state = builder.CreateAlloca(stateType, nullptr, "state");

        // create memory type
        auto memoryType = llvm::Type::getInt8PtrTy(context);

        // create alloca instruction for Memory
        auto memory = builder.CreateAlloca(memoryType, nullptr, "memory");
        
        // Create call to destination function and pass parameters: 
        // state, program_counter, memory, e.g.:
        // define ptr @sub_1400016d0.1(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {

        // Get the target function
        auto targetFunc = M.getFunction(TargetFuncName);
        if (!targetFunc) {
            LOG(ERROR) << "Failed to find target function: " << TargetFuncName;
            return nullptr;
        }
        
        // Create call instruction
        auto pcVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), PC);
        builder.CreateCall(targetFunc, {state, pcVal, memory});

        // todo: return rax?
        // Create return instruction
        builder.CreateRetVoid();
    }

    // validate the module
    VLOG(1) << "Validating module";
    std::string verify_err;
    llvm::raw_string_ostream errStream(verify_err);
    if (llvm::verifyModule(M, &errStream)) {
        LOG(ERROR) << "Failed to verify module: " << verify_err;
        return nullptr;
    }

    return mainFunc;
}

llvm::Function* CreateEntryFunction(
    llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName)
{
    auto& context = M.getContext();

    // Create function type for entry: void entry()
    auto voidTy = llvm::Type::getVoidTy(context);
    auto funcTy = llvm::FunctionType::get(voidTy, false);
    
    // Create the entry function
    auto func = llvm::Function::Create(
        funcTy, 
        llvm::Function::ExternalLinkage,
        "entry", 
        &M);

    // Create entry basic block
    auto entryBB = llvm::BasicBlock::Create(context, "entry", func);
    llvm::IRBuilder<> builder(entryBB);

    // Get the utility functions
    auto setGSBaseFunc = M.getFunction("SetGSBase");
    auto setParamsFunc = M.getFunction("SetParameters");
    auto setStackFunc = M.getFunction("SetStack");
    auto setPCFunc = M.getFunction("SetPC");

    if (!setGSBaseFunc || !setParamsFunc || !setStackFunc || !setPCFunc) {
        LOG(ERROR) << "Failed to find utility functions:"
                  << (!setGSBaseFunc ? " SetGSBase" : "")
                  << (!setParamsFunc ? " SetParameters" : "")
                  << (!setStackFunc ? " SetStack" : "")
                  << (!setPCFunc ? " SetPC" : "");
        func->eraseFromParent();
        return nullptr;
    }

    // Call SetGSBase with GSBase parameter
    auto gsBaseVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), GSBase);
    builder.CreateCall(setGSBaseFunc, {gsBaseVal});

    // Call SetParameters
    builder.CreateCall(setParamsFunc);

    // Call SetStack
    builder.CreateCall(setStackFunc);

    // Call SetPC with PC parameter
    auto pcVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), PC);
    builder.CreateCall(setPCFunc, {pcVal});

    // Get the target function
    auto targetFunc = M.getFunction(TargetFuncName);
    if (!targetFunc) {
        LOG(ERROR) << "Failed to find target function: " << TargetFuncName;
        func->eraseFromParent();
        return nullptr;
    }

    // Get global variables
    auto stateGlobal = M.getGlobalVariable("State");
    auto memoryGlobal = M.getGlobalVariable("Memory");
    auto globalPCGlobal = M.getGlobalVariable("GlobalPC");

    if (!stateGlobal || !memoryGlobal || !globalPCGlobal) {
        LOG(ERROR) << "Failed to find required globals:"
                  << (!stateGlobal ? " State" : "")
                  << (!memoryGlobal ? " Memory" : "")
                  << (!globalPCGlobal ? " GlobalPC" : "");
        func->eraseFromParent();
        return nullptr;
    }

    // Get state pointer
    auto statePtr = builder.CreateBitCast(
        builder.CreateConstGEP1_32(stateGlobal->getValueType(), stateGlobal, 0),
        builder.getInt8PtrTy());

    // Load memory pointer
    auto memoryPtr = builder.CreateLoad(memoryGlobal->getValueType(), memoryGlobal);

    // Load PC value
    auto globalPC = builder.CreateLoad(globalPCGlobal->getValueType(), globalPCGlobal);

    // Call the target function with our constructed arguments
    builder.CreateCall(targetFunc, {statePtr, globalPC, memoryPtr});

    // Add return instruction
    builder.CreateRetVoid();

    // First verify the function
    std::string verify_err;
    llvm::raw_string_ostream errStream(verify_err);
    if (llvm::verifyFunction(*func, &errStream)) {
        LOG(ERROR) << "Failed to verify entry function: " << verify_err;
        func->eraseFromParent();
        return nullptr;
    }

    // Then verify the entire module
    verify_err.clear();
    if (llvm::verifyModule(M, &errStream)) {
        LOG(ERROR) << "Merged module is not valid: " << verify_err;
        func->eraseFromParent();
        return nullptr;
    }

    return func;
}

llvm::Function* CreateEntryWithState(
    llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName, const std::string& llFile)
{
    // Then create the entry function
    return CreateEntryFunction(M, PC, GSBase, TargetFuncName);
}

} // namespace BitcodeManipulation