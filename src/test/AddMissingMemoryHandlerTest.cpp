#include <gtest/gtest.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <glog/logging.h>

#include "BitcodeManipulation/AddMissingMemory.h"
#include "BitcodeManipulation/AddMissingMemoryHandler.h"
#include "BitcodeManipulation/AddMissingBlockHandler.h"
#include "BitcodeManipulation/InsertLogging.h"
#include "BitcodeManipulation/MiscUtils.h"
#include "JIT/JITEngine.h"
#include "JIT/JITRuntime.h"
#include "Prebuilt/Utils.h"

class AddMissingMemoryHandlerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
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
        
        // Clear any previous missing memory
        Runtime::MissingMemoryTracker::ClearMissingMemory();
    }

    void TearDown() override {
        Context.reset();
        Module.reset();
    }

    // Helper to create a test function that calls __remill_read_memory_32
    llvm::Function* CreateTestFunction(uint64_t addr) {
        llvm::IRBuilder<> Builder(*Context);
        
        // Create function type for our test function: void*(void*, uint64_t, void*)
        auto* FuncTy = llvm::FunctionType::get(
            llvm::Type::getInt8PtrTy(*Context),
            {
                llvm::Type::getInt8PtrTy(*Context),
                llvm::Type::getInt64Ty(*Context),
                llvm::Type::getInt8PtrTy(*Context)
            },
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
        
        // Get function arguments
        auto state = TestFunc->arg_begin();
        auto pc = std::next(state);
        auto memory = std::next(pc);
        
        // Create call to __remill_read_memory_32
        auto* ReadMemoryFunc = Module->getOrInsertFunction(
            "__remill_read_memory_32",
            llvm::FunctionType::get(
                llvm::Type::getInt32Ty(*Context),
                {
                    llvm::Type::getInt8PtrTy(*Context),
                    llvm::Type::getInt64Ty(*Context)
                },
                false
            )
        ).getCallee();
        
        // Create constant for address value
        auto* AddrVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*Context), addr);
        
        // Call __remill_read_memory_32
        std::vector<llvm::Value*> Args = {memory, AddrVal};
        Builder.CreateCall(llvm::cast<llvm::Function>(ReadMemoryFunc), Args);
        
        // Return memory pointer
        Builder.CreateRet(memory);
        
        return TestFunc;
    }

    std::unique_ptr<llvm::LLVMContext> Context;
    std::unique_ptr<llvm::Module> Module;
};

TEST_F(AddMissingMemoryHandlerTest, TestMemoryRead32) {
    const uint64_t TEST_ADDR = 0x1234;
    const std::vector<uint8_t> TEST_PAGE = {
        0x11, 0x22, 0x33, 0x44,  // First 4 bytes will be read as uint32_t
        0x55, 0x66, 0x77, 0x88,  // Rest of the page...
        // Fill rest with zeros
    };
    
    // Pad the page to PREBUILT_MEMORY_CELL_SIZE
    std::vector<uint8_t> padded_page = TEST_PAGE;
    padded_page.resize(PREBUILT_MEMORY_CELL_SIZE, 0);
    
    // First iteration - should trigger missing memory
    {
        // Create test function that reads memory at TEST_ADDR
        auto* TestFunc = CreateTestFunction(TEST_ADDR);
        ASSERT_NE(TestFunc, nullptr);
        
        // Create entry point that sets up state
        auto* EntryFunc = BitcodeManipulation::CreateEntryWithState(*Module, 0x1000, 0x2000, "test_func");
        LOG(INFO) << "Entry point created";
        ASSERT_NE(EntryFunc, nullptr);
        
        // Create the memory lookup function
        auto* GetMemoryPtr = BitcodeManipulation::CreateGetSavedMemoryPtr(*Module);
        LOG(INFO) << "Memory lookup function created";
        ASSERT_NE(GetMemoryPtr, nullptr);

        BitcodeManipulation::AddMissingBlockHandler(*Module, {});
        BitcodeManipulation::InsertFunctionLogging(*Module);

        // dump module
        BitcodeManipulation::DumpModule(*Module, "test_module_1.ll");
        
        // Initialize JIT and execute
        JITEngine jit;
        ASSERT_TRUE(jit.Initialize(std::move(Module)));
        LOG(INFO) << "JIT initialized";
        ASSERT_TRUE(jit.ExecuteFunction("entry"));
        LOG(INFO) << "Entry function executed";
        
        // Verify missing memory was tracked
        const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
        ASSERT_EQ(missing_memory.size(), 1);
        ASSERT_EQ(missing_memory[0].first, TEST_ADDR & ~(PREBUILT_MEMORY_CELL_SIZE - 1));  // Should be page aligned
        ASSERT_EQ(missing_memory[0].second, 4);  // 4 bytes for uint32_t
        
        // Clear for next iteration
        Runtime::MissingMemoryTracker::ClearMissingMemory();
    }
    
    // Second iteration with mapped memory
    {
        // Create new module and test function
        Module = std::make_unique<llvm::Module>("test_module_2", *Context);
        auto* TestFunc = CreateTestFunction(TEST_ADDR);
        ASSERT_NE(TestFunc, nullptr);
        
        // Create entry point that sets up state
        auto* EntryFunc = BitcodeManipulation::CreateEntryWithState(*Module, 0x1000, 0x2000, "test_func");
        ASSERT_NE(EntryFunc, nullptr);
        
        // Add the memory page
        ASSERT_TRUE(BitcodeManipulation::AddMissingMemory(*Module, TEST_ADDR & ~(PREBUILT_MEMORY_CELL_SIZE - 1), padded_page));
        
        // Create the memory lookup function
        auto* GetMemoryPtr = BitcodeManipulation::CreateGetSavedMemoryPtr(*Module);
        ASSERT_NE(GetMemoryPtr, nullptr);
        
        BitcodeManipulation::AddMissingBlockHandler(*Module, {});
        BitcodeManipulation::InsertFunctionLogging(*Module);

        // dump module
        BitcodeManipulation::DumpModule(*Module, "test_module_2.ll");

        // Initialize JIT and execute
        JITEngine jit;
        ASSERT_TRUE(jit.Initialize(std::move(Module)));
        ASSERT_TRUE(jit.ExecuteFunction("entry"));
        
        // Verify no missing memory was tracked
        const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
        ASSERT_EQ(missing_memory.size(), 0);
    }
}

TEST_F(AddMissingMemoryHandlerTest, TestMemoryReadBoundaryCross32) {
    // Set address to page boundary - 2, so a 32-bit read will cross pages
    const uint64_t TEST_ADDR = 0x2000 - 2;
    
    // Create test data for first page (ending with 0x1FFF)
    std::vector<uint8_t> FIRST_PAGE(PREBUILT_MEMORY_CELL_SIZE, 0);
    // Set the last two bytes of the first page
    FIRST_PAGE[PREBUILT_MEMORY_CELL_SIZE - 2] = 0x11;
    FIRST_PAGE[PREBUILT_MEMORY_CELL_SIZE - 1] = 0x22;
    
    // Create test data for second page (starting with 0x2000)
    std::vector<uint8_t> SECOND_PAGE(PREBUILT_MEMORY_CELL_SIZE, 0);
    // Set the first two bytes of the second page
    SECOND_PAGE[0] = 0x33;
    SECOND_PAGE[1] = 0x44;
    
    // First iteration - should trigger missing memory for both pages
    {
        // Create test function that reads memory at TEST_ADDR
        auto* TestFunc = CreateTestFunction(TEST_ADDR);
        ASSERT_NE(TestFunc, nullptr);
        
        // Create entry point that sets up state
        auto* EntryFunc = BitcodeManipulation::CreateEntryWithState(*Module, 0x1000, 0x2000, "test_func");
        LOG(INFO) << "Entry point created";
        ASSERT_NE(EntryFunc, nullptr);
        
        // Create the memory lookup function
        auto* GetMemoryPtr = BitcodeManipulation::CreateGetSavedMemoryPtr(*Module);
        LOG(INFO) << "Memory lookup function created";
        ASSERT_NE(GetMemoryPtr, nullptr);

        BitcodeManipulation::AddMissingBlockHandler(*Module, {});
        BitcodeManipulation::InsertFunctionLogging(*Module);

        // dump module
        BitcodeManipulation::DumpModule(*Module, "test_boundary_cross_1.ll");
        
        // Initialize JIT and execute
        JITEngine jit;
        ASSERT_TRUE(jit.Initialize(std::move(Module)));
        LOG(INFO) << "JIT initialized";
        ASSERT_TRUE(jit.ExecuteFunction("entry"));
        LOG(INFO) << "Entry function executed";
        
        // Verify missing memory was tracked - should have two entries
        const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
        ASSERT_EQ(missing_memory.size(), 2);
        
        // First page starting address (aligned to PREBUILT_MEMORY_CELL_SIZE)
        uint64_t first_page_addr = (TEST_ADDR) & ~(PREBUILT_MEMORY_CELL_SIZE - 1);
        // Second page starting address
        uint64_t second_page_addr = 0x2000;
        
        // Check that both pages were detected as missing
        bool found_first_page = false;
        bool found_second_page = false;
        
        for (const auto& entry : missing_memory) {
            if (entry.first == first_page_addr) {
                found_first_page = true;
            } else if (entry.first == second_page_addr) {
                found_second_page = true;
            }
        }
        
        ASSERT_TRUE(found_first_page) << "First page not detected as missing";
        ASSERT_TRUE(found_second_page) << "Second page not detected as missing";
        
        // Clear for next iteration
        Runtime::MissingMemoryTracker::ClearMissingMemory();
    }
    
    // Second iteration with mapped memory for both pages
    {
        // Create new module and test function
        Module = std::make_unique<llvm::Module>("test_module_boundary_cross_2", *Context);
        auto* TestFunc = CreateTestFunction(TEST_ADDR);
        ASSERT_NE(TestFunc, nullptr);
        
        // Create entry point that sets up state
        auto* EntryFunc = BitcodeManipulation::CreateEntryWithState(*Module, 0x1000, 0x2000, "test_func");
        ASSERT_NE(EntryFunc, nullptr);
        
        // Calculate page addresses
        uint64_t first_page_addr = (TEST_ADDR) & ~(PREBUILT_MEMORY_CELL_SIZE - 1);
        uint64_t second_page_addr = 0x2000;
        
        // Add both memory pages
        ASSERT_TRUE(BitcodeManipulation::AddMissingMemory(*Module, first_page_addr, FIRST_PAGE));
        ASSERT_TRUE(BitcodeManipulation::AddMissingMemory(*Module, second_page_addr, SECOND_PAGE));
        
        // Create the memory lookup function
        auto* GetMemoryPtr = BitcodeManipulation::CreateGetSavedMemoryPtr(*Module);
        ASSERT_NE(GetMemoryPtr, nullptr);
        
        BitcodeManipulation::AddMissingBlockHandler(*Module, {});
        BitcodeManipulation::InsertFunctionLogging(*Module);

        // dump module
        BitcodeManipulation::DumpModule(*Module, "test_boundary_cross_2.ll");

        // Initialize JIT and execute
        JITEngine jit;
        ASSERT_TRUE(jit.Initialize(std::move(Module)));
        ASSERT_TRUE(jit.ExecuteFunction("entry"));
        
        // Verify no missing memory was tracked
        const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
        ASSERT_EQ(missing_memory.size(), 0);
    }
} 