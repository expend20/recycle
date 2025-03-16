#include "ReplaceStackMemoryWrites.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <glog/logging.h>

#include <vector>
#include <unordered_map>

namespace BitcodeManipulation {

namespace {

// Check if the value is based on the Stack variable
bool isStackBasedCalculation(llvm::Value *Value, llvm::GlobalVariable *StackVar) {
    // If it's a constant expression
    if (llvm::ConstantExpr *CE = llvm::dyn_cast<llvm::ConstantExpr>(Value)) {
        if (CE->getOpcode() == llvm::Instruction::Add || 
            CE->getOpcode() == llvm::Instruction::Sub) {
            return isStackBasedCalculation(CE->getOperand(0), StackVar) || 
                   isStackBasedCalculation(CE->getOperand(1), StackVar);
        } else if (CE->getOpcode() == llvm::Instruction::PtrToInt) {
            return CE->getOperand(0) == StackVar;
        }
    }
    
    // If it's an instruction
    if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(Value)) {
        if (I->getOpcode() == llvm::Instruction::Add || 
            I->getOpcode() == llvm::Instruction::Sub) {
            return isStackBasedCalculation(I->getOperand(0), StackVar) || 
                   isStackBasedCalculation(I->getOperand(1), StackVar);
        } else if (I->getOpcode() == llvm::Instruction::PtrToInt) {
            return I->getOperand(0) == StackVar;
        }
    }
    
    // If it's the Stack variable itself
    return Value == StackVar;
}

// Evaluate constant expression to get the offset from Stack base
int64_t evaluateStackOffset(llvm::Value *Value, llvm::GlobalVariable *StackVar) {
    // Handle constant integers
    if (llvm::ConstantInt *CI = llvm::dyn_cast<llvm::ConstantInt>(Value)) {
        return CI->getSExtValue();
    }
    
    // Handle constant expressions
    if (llvm::ConstantExpr *CE = llvm::dyn_cast<llvm::ConstantExpr>(Value)) {
        if (CE->getOpcode() == llvm::Instruction::Add) {
            return evaluateStackOffset(CE->getOperand(0), StackVar) + 
                   evaluateStackOffset(CE->getOperand(1), StackVar);
        } else if (CE->getOpcode() == llvm::Instruction::Sub) {
            return evaluateStackOffset(CE->getOperand(0), StackVar) - 
                   evaluateStackOffset(CE->getOperand(1), StackVar);
        } else if (CE->getOpcode() == llvm::Instruction::PtrToInt) {
            return (CE->getOperand(0) == StackVar) ? 0 : 0;
        }
    }
    
    // Handle instructions
    if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(Value)) {
        if (I->getOpcode() == llvm::Instruction::Add) {
            return evaluateStackOffset(I->getOperand(0), StackVar) + 
                   evaluateStackOffset(I->getOperand(1), StackVar);
        } else if (I->getOpcode() == llvm::Instruction::Sub) {
            return evaluateStackOffset(I->getOperand(0), StackVar) - 
                   evaluateStackOffset(I->getOperand(1), StackVar);
        } else if (I->getOpcode() == llvm::Instruction::PtrToInt) {
            return (I->getOperand(0) == StackVar) ? 0 : 0;
        }
    }
    
    // Cannot evaluate the offset
    return 0;
}

// Get bit width from function name
unsigned getBitWidthFromFunctionName(const std::string &FunctionName) {
    // Extract the bit width from the function name (e.g., "__remill_write_memory_64" -> 64)
    size_t underscorePos = FunctionName.rfind('_');
    if (underscorePos != std::string::npos) {
        std::string widthStr = FunctionName.substr(underscorePos + 1);
        return std::stoi(widthStr);
    }
    return 0;  // Default to 0 if not found
}

} // anonymous namespace

bool ReplaceStackMemoryWrites(
    llvm::Module& Module, 
    const std::string& StackVariableName,
    const std::vector<std::string>& MemWriteFunctions) {
    
    // Get the Stack variable
    llvm::GlobalVariable *StackVar = Module.getNamedGlobal(StackVariableName);
    if (!StackVar) {
        LOG(ERROR) << "Stack variable '" << StackVariableName << "' not found in module";
        return false;
    }
    
    // Get the LLVM context
    llvm::LLVMContext &Context = Module.getContext();
    
    // Store calls to replace to avoid iterator invalidation
    std::vector<std::pair<llvm::CallInst*, unsigned>> CallsToReplace;
    
    // Find and prepare functions
    std::unordered_map<std::string, llvm::Function*> WriteMemFuncs;
    for (const auto &FuncName : MemWriteFunctions) {
        llvm::Function *Func = Module.getFunction(FuncName);
        if (Func) {
            WriteMemFuncs[FuncName] = Func;
        }
    }
    
    if (WriteMemFuncs.empty()) {
        LOG(ERROR) << "No __remill_write_memory_* functions found in module";
        return false;
    }
    
    // Find all calls to any of the write memory functions
    for (auto &F : Module) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (llvm::CallInst *Call = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    llvm::Function *CalledFunc = Call->getCalledFunction();
                    
                    // Check if this is one of our target functions
                    for (const auto &Entry : WriteMemFuncs) {
                        if (CalledFunc == Entry.second) {
                            // Check if the second argument is based on Stack variable
                            llvm::Value *AddrArg = Call->getArgOperand(1);
                            if (isStackBasedCalculation(AddrArg, StackVar)) {
                                unsigned BitWidth = getBitWidthFromFunctionName(Entry.first);
                                CallsToReplace.push_back({Call, BitWidth});
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // Replace each call with appropriate GEP and store instructions
    for (const auto &CallInfo : CallsToReplace) {
        llvm::CallInst *Call = CallInfo.first;
        unsigned BitWidth = CallInfo.second;
        
        llvm::IRBuilder<> Builder(Call);
        
        // Get the address and value arguments
        llvm::Value *MemoryArg = Call->getArgOperand(0);  // Memory context
        llvm::Value *AddrArg = Call->getArgOperand(1);    // Address
        llvm::Value *ValueArg = Call->getArgOperand(2);   // Value to store
        
        // Calculate the stack offset
        int64_t Offset = evaluateStackOffset(AddrArg, StackVar);
        
        // Create GEP to index into the Stack array
        llvm::Value *Indices[] = {
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(Context), 0),
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(Context), Offset)
        };
        
        llvm::Value *GEP = Builder.CreateInBoundsGEP(
            StackVar->getValueType(), StackVar, 
            llvm::ArrayRef<llvm::Value*>(Indices, 2));
        
        // The Stack array is an i8 array, so store into it as i8* type
        llvm::Type *Int8PtrTy = llvm::Type::getInt8PtrTy(Context);
        
        // Get appropriate integer type based on bit width
        llvm::Type *IntTy;
        switch (BitWidth) {
            case 8:
                IntTy = llvm::Type::getInt8Ty(Context);
                break;
            case 16:
                IntTy = llvm::Type::getInt16Ty(Context);
                break;
            case 32:
                IntTy = llvm::Type::getInt32Ty(Context);
                break;
            case 64:
            default:  // Default to 64-bit if unknown
                IntTy = llvm::Type::getInt64Ty(Context);
                break;
        }
        
        // Convert the GEP to a known type (i8*)
        llvm::Value *CastedGEP = Builder.CreateBitCast(GEP, Int8PtrTy);
        
        // Make sure the value is properly typed
        if (ValueArg->getType() != IntTy) {
            ValueArg = Builder.CreateBitCast(ValueArg, IntTy);
        }
        
        // Store the value to the calculated address
        llvm::StoreInst *Store = Builder.CreateStore(ValueArg, CastedGEP);
        
        // Replace all uses of the call with the memory context (first parameter)
        // This is what __remill_write_memory_* would return
        Call->replaceAllUsesWith(MemoryArg);
        
        // Remove the original call
        Call->eraseFromParent();
        
        VLOG(1) << "Replaced __remill_write_memory_" << BitWidth 
                << " call with GEP instruction at offset " << Offset;
    }
    
    return !CallsToReplace.empty();
}

} // namespace BitcodeManipulation 