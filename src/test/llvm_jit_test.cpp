#include <iostream>
#include <memory>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

const int multiply_factor = 3;

// Native function we'll call from JIT-compiled code
extern "C" int multiply(int x) {
    return x * multiply_factor;
}

int main() {
    // Initialize LLVM's target infrastructure
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Create a new context and module
    LLVMContext Context;
    std::unique_ptr<Module> TheModule = std::make_unique<Module>("JITTest", Context);
    
    // Create an IRBuilder
    IRBuilder<> Builder(Context);
    
    // Create function type: int (int, int)
    std::vector<Type*> Ints(2, Type::getInt32Ty(Context));
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(Context), Ints, false);
    
    // Declare the external multiply function
    std::vector<Type*> MultiplyArgs(1, Type::getInt32Ty(Context));
    FunctionType *MultiplyFT = FunctionType::get(Type::getInt32Ty(Context), MultiplyArgs, false);
    Function *MultiplyF = Function::Create(MultiplyFT, Function::ExternalLinkage, "multiply", TheModule.get());
    
    // Create function: int add(int a, int b)
    Function *F = Function::Create(FT, Function::ExternalLinkage, "add", TheModule.get());
    
    // Name the function arguments
    auto args_it = F->arg_begin();
    Value *Arg1 = args_it++;
    Value *Arg2 = args_it;
    Arg1->setName("a");
    Arg2->setName("b");
    
    // Create a new basic block
    BasicBlock *BB = BasicBlock::Create(Context, "entry", F);
    Builder.SetInsertPoint(BB);
    
    // Create the add instruction
    Value *Result = Builder.CreateAdd(Arg1, Arg2, "addtmp");
    
    // Call the multiply function with the result of add
    std::vector<Value*> CallArgs;
    CallArgs.push_back(Result);
    Result = Builder.CreateCall(MultiplyF, CallArgs, "multmp");
    
    // Create the return instruction
    Builder.CreateRet(Result);
    
    // Verify the function
    std::string ErrorStr;
    raw_string_ostream ErrorOS(ErrorStr);
    if (verifyModule(*TheModule, &ErrorOS)) {
        std::cerr << "Error verifying module: " << ErrorStr << std::endl;
        return 1;
    }
    
    // Create the JIT
    std::string ErrStr;
    ExecutionEngine *EE = EngineBuilder(std::move(TheModule))
        .setErrorStr(&ErrStr)
        .create();
        
    if (!EE) {
        std::cerr << "Failed to create execution engine: " << ErrStr << std::endl;
        return 1;
    }
    
    // Map the external function
    EE->addGlobalMapping(MultiplyF, reinterpret_cast<void*>(multiply));
    
    // Get the function pointer
    typedef int (*AddFnPtr)(int, int);
    AddFnPtr AddFn = reinterpret_cast<AddFnPtr>(EE->getFunctionAddress("add"));
    
    if (!AddFn) {
        std::cerr << "Failed to get function pointer" << std::endl;
        return 1;
    }
    
    // Test the function
    int x = 5, y = 7;
    int result = AddFn(x, y);
    std::cout << x << " + " << y << " = " << result << std::endl;
    
    delete EE;
    return 0;
} 