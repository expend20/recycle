#include <gtest/gtest.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <glog/logging.h>

#include "BitcodeManipulation/AddMissingBlockHandler.h"
#include "BitcodeManipulation/MiscUtils.h"
#include "JIT/JITEngine.h"
#include "JIT/JITRuntime.h"

class AddMissingBlockHandlerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Initialize Google logging
        google::InitGoogleLogging("AddMissingBlockHandlerTest");
        FLAGS_logtostderr = true;
        
        // Initialize LLVM
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        
        // Initialize MCJIT
        LLVMLinkInMCJIT();
    }

    void SetUp() override {
        // Create new context and module for each test
        Context = std::make_unique<llvm::LLVMContext>();
        Module = std::make_unique<llvm::Module>("test_module", *Context);
        
        // Clear any previous missing blocks
        Runtime::MissingBlockTracker::ClearMissingBlocks();
    }

    void TearDown() override {
        Context.reset();
        Module.reset();
    }

    // Helper to create a test function that calls __remill_missing_block
    llvm::Function* CreateTestFunction(uint64_t pc_value) {
        llvm::IRBuilder<> Builder(*Context);
        
        // Create function type for our test function: void()
        auto* FuncTy = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*Context),
            false
        );
        
        // Create the test function
        auto* TestFunc = llvm::Function::Create(
            FuncTy,
            llvm::GlobalValue::ExternalLinkage,
            "test_func",
            Module.get()
        );
        
        // Create entry block
        auto* EntryBB = llvm::BasicBlock::Create(*Context, "entry", TestFunc);
        Builder.SetInsertPoint(EntryBB);
        
        // Create call to __remill_missing_block
        auto* MissingBlockFunc = Module->getOrInsertFunction(
            "__remill_missing_block",
            llvm::FunctionType::get(
                llvm::Type::getInt8PtrTy(*Context),
                {
                    llvm::Type::getInt8PtrTy(*Context),
                    llvm::Type::getInt64Ty(*Context),
                    llvm::Type::getInt8PtrTy(*Context)
                },
                false
            )
        ).getCallee();
        
        // Create null pointers for state and memory parameters
        auto* NullPtr = llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(*Context));
        
        // Create constant for PC value
        auto* PC = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*Context), pc_value);
        
        // Call __remill_missing_block
        std::vector<llvm::Value*> Args = {NullPtr, PC, NullPtr};
        Builder.CreateCall(llvm::cast<llvm::Function>(MissingBlockFunc), Args);
        
        // Return void
        Builder.CreateRetVoid();
        
        return TestFunc;
    }

    // Helper to create a stub function with remill's signature
    llvm::Function* CreateStubFunction(const std::string& name) {
        // Create function type matching remill's signature: void*(void*, uint64_t, void*)
        auto* FuncTy = llvm::FunctionType::get(
            llvm::Type::getInt8PtrTy(*Context),
            {
                llvm::Type::getInt8PtrTy(*Context),
                llvm::Type::getInt64Ty(*Context),
                llvm::Type::getInt8PtrTy(*Context)
            },
            false
        );
        
        // Create the function
        auto* Func = llvm::Function::Create(
            FuncTy,
            llvm::GlobalValue::ExternalLinkage,
            name,
            Module.get()
        );
        
        // Create entry block that just returns the memory parameter
        auto* EntryBB = llvm::BasicBlock::Create(*Context, "entry", Func);
        llvm::IRBuilder<> Builder(EntryBB);
        
        // Return the memory parameter (third argument)
        Builder.CreateRet(Func->getArg(2));
        
        return Func;
    }

    std::unique_ptr<llvm::LLVMContext> Context;
    std::unique_ptr<llvm::Module> Module;
};

TEST_F(AddMissingBlockHandlerTest, TestMissingBlockHandlerUpdate) {
    const uint64_t TEST_PC_1 = 0x1234;
    const uint64_t TEST_PC_2 = 0x5678;
    
    // First iteration
    {
        // Create test function that calls missing block with TEST_PC_1
        CreateTestFunction(TEST_PC_1);
        
        // Add missing block handler with empty mapping
        std::vector<std::pair<uint64_t, std::string>> addr_to_func_map;
        BitcodeManipulation::AddMissingBlockHandler(*Module, addr_to_func_map);

        // dump module
        // BitcodeManipulation::DumpModule(*Module, "test_module_1.ll");
        
        // Initialize JIT and execute
        JITEngine jit;
        ASSERT_TRUE(jit.Initialize(std::move(Module)));
        ASSERT_TRUE(jit.ExecuteFunction("test_func"));
        LOG(INFO) << "Missing blocks: " << Runtime::MissingBlockTracker::GetMissingBlocks().size();
        
        // Verify missing block was tracked
        const auto& missing_blocks = Runtime::MissingBlockTracker::GetMissingBlocks();
        ASSERT_EQ(missing_blocks.size(), 1);
        ASSERT_EQ(missing_blocks[0], TEST_PC_1);
        
        // Clear for next iteration
        Runtime::MissingBlockTracker::ClearMissingBlocks();
    }
    
    // Second iteration with updated mapping
    {
        // Create new module and test function for second iteration
        Module = std::make_unique<llvm::Module>("test_module_2", *Context);
        CreateTestFunction(TEST_PC_2);
        
        // Create the stub function that will handle TEST_PC_1
        CreateStubFunction("sub_1234");
        
        // Add missing block handler with updated mapping that includes first PC
        std::vector<std::pair<uint64_t, std::string>> addr_to_func_map = {
            {TEST_PC_1, "sub_1234"}
        };
        BitcodeManipulation::AddMissingBlockHandler(*Module, addr_to_func_map);
        
        // Dump module before moving it to JIT
        LOG(INFO) << "Dumping module before second execution";
        //BitcodeManipulation::DumpModule(*Module, "test_module_2.ll");
        
        // Initialize JIT and execute
        JITEngine jit;
        ASSERT_TRUE(jit.Initialize(std::move(Module)));
        LOG(INFO) << "Calling test_func second time";
        ASSERT_TRUE(jit.ExecuteFunction("test_func"));
        LOG(INFO) << "Missing blocks: " << Runtime::MissingBlockTracker::GetMissingBlocks().size();
        
        // Verify only the new PC was tracked
        const auto& missing_blocks = Runtime::MissingBlockTracker::GetMissingBlocks();
        ASSERT_EQ(missing_blocks.size(), 1);
        ASSERT_EQ(missing_blocks[0], TEST_PC_2);
    }
} 