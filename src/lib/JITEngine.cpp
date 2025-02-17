#include "JITEngine.h"
#include "Runtime.h"
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Host.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Verifier.h>
#include <glog/logging.h>

JITEngine::JITEngine() {}

JITEngine::~JITEngine() {}

bool JITEngine::Initialize() {
    // Initialize LLVM's target infrastructure
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    return true;
}

bool JITEngine::InitializeWithModule(std::unique_ptr<llvm::Module> module) {
    std::string ErrStr;

    // Verify the module first
    if (llvm::verifyModule(*module, &llvm::errs())) {
        LOG(ERROR) << "Module verification failed";
        return false;
    }

    // Log all functions in module before JIT
    LOG(INFO) << "Functions in module before JIT:";
    for (auto &F : *module) {
        LOG(INFO) << "  " << F.getName().str() << " (address: " << &F << ")";
    }

    // Get target machine first
    std::unique_ptr<llvm::TargetMachine> TM(
        llvm::EngineBuilder().selectTarget()
    );
    
    if (!TM) {
        LOG(ERROR) << "Failed to select target";
        return false;
    }

    // Set the data layout before creating the engine
    module->setDataLayout(TM->createDataLayout());
    
    // Dump module before JIT compilation
    std::error_code EC;
    llvm::raw_fd_ostream file("lifted_jit.ll", EC);
    if (!EC) {
        module->print(file, nullptr);
        file.close();
        LOG(INFO) << "JIT module written to lifted_jit.ll";
    }

    // Create the execution engine with MCJIT
    ExecutionEngine = std::unique_ptr<llvm::ExecutionEngine>(
        llvm::EngineBuilder(std::move(module))
        .setErrorStr(&ErrStr)
        .setEngineKind(llvm::EngineKind::JIT)
        .setMCPU(llvm::sys::getHostCPUName())
        .setOptLevel(llvm::CodeGenOpt::None)  // Disable optimizations for now
        .create(TM.release())
    );

    if (!ExecutionEngine) {
        LOG(ERROR) << "Failed to create execution engine: " << ErrStr;
        return false;
    }

    // First, finalize the object to generate code for all functions
    LOG(INFO) << "Finalizing execution engine...";
    //ExecutionEngine->finalizeObject();

    // Map runtime functions after finalization
    LOG(INFO) << "Mapping runtime functions:";
    struct RuntimeFunc {
        std::string name;
        void* addr;
        bool isNoReturn;
    };

    std::vector<RuntimeFunc> runtimeFuncs = {
        {"__remill_log_function", reinterpret_cast<void*>(&Runtime::__remill_log_function), true},
        {"__remill_missing_block", reinterpret_cast<void*>(&Runtime::__remill_missing_block), false},
        {"__remill_write_memory_64", reinterpret_cast<void*>(&Runtime::__remill_write_memory_64), false}
    };

    // First get all JIT'd function addresses
    std::unordered_map<std::string, uint64_t> jitAddresses;
    for (const auto& func : runtimeFuncs) {
        if (auto* F = ExecutionEngine->FindFunctionNamed(func.name)) {
            uint64_t addr = ExecutionEngine->getFunctionAddress(func.name);
            jitAddresses[func.name] = addr;
            LOG(INFO) << "JIT'd address for " << func.name << ": 0x" << std::hex << addr;
        }
    }

    // Now map the runtime functions
    for (const auto& func : runtimeFuncs) {
        if (auto* F = ExecutionEngine->FindFunctionNamed(func.name)) {
            LOG(INFO) << "Found " << func.name << ", mapping to " << std::hex << reinterpret_cast<uintptr_t>(func.addr);
            ExecutionEngine->updateGlobalMapping(F, func.addr);
            
            // Verify the mapping
            void* MappedAddr = ExecutionEngine->getPointerToGlobalIfAvailable(F);
            LOG(INFO) << "Verified " << func.name << " mapped to 0x" << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
            
            if (MappedAddr != func.addr) {
                LOG(ERROR) << "Mapping verification failed for " << func.name;
                LOG(ERROR) << "Expected: 0x" << std::hex << reinterpret_cast<uintptr_t>(func.addr);
                LOG(ERROR) << "Got: 0x" << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
            }
        }
    }

    // Now map sub_1400016d0 to sub_1400016d0.1
    if (auto* Src = ExecutionEngine->FindFunctionNamed("sub_1400016d0")) {
        uint64_t DstAddr = ExecutionEngine->getFunctionAddress("sub_1400016d0.1");
        if (DstAddr) {
            LOG(INFO) << "Mapping sub_1400016d0 to sub_1400016d0.1 at 0x" << std::hex << DstAddr;
            ExecutionEngine->updateGlobalMapping(Src, reinterpret_cast<void*>(DstAddr));
            
            // Verify the mapping
            void* MappedAddr = ExecutionEngine->getPointerToGlobalIfAvailable(Src);
            LOG(INFO) << "Verified sub_1400016d0 mapped to 0x" << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
        } else {
            LOG(ERROR) << "Failed to get address for sub_1400016d0.1";
        }
    }

    // Final verification of all function addresses
    LOG(INFO) << "Final verification of function addresses:";
    for (const auto& func : runtimeFuncs) {
        if (auto* F = ExecutionEngine->FindFunctionNamed(func.name)) {
            void* MappedAddr = ExecutionEngine->getPointerToGlobalIfAvailable(F);
            LOG(INFO) << func.name << " final address: 0x" << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
            if (!MappedAddr) {
                LOG(ERROR) << "Function " << func.name << " has null address in final verification!";
            }
        }
    }

    return true;
}

void JITEngine::AddExternalMapping(llvm::Function* F, void* Addr) {
    if (ExecutionEngine) {
        LOG(INFO) << "Mapping function " << F->getName().str() << " to address " << std::hex << reinterpret_cast<uintptr_t>(Addr);
        ExecutionEngine->updateGlobalMapping(F, Addr);
        
        // Verify the mapping
        void* MappedAddr = ExecutionEngine->getPointerToGlobalIfAvailable(F);
        LOG(INFO) << "Verified mapping: " << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
    }
}

void JITEngine::AddExternalMappingByName(const std::string& name, void* Addr) {
    if (!ExecutionEngine) {
        LOG(ERROR) << "Execution engine not initialized when trying to map " << name;
        return;
    }

    // Try to find the function in the module
    if (auto* F = ExecutionEngine->FindFunctionNamed(name)) {
        LOG(INFO) << "Found function " << name << " in module, mapping to address " << std::hex << reinterpret_cast<uintptr_t>(Addr);
        
        // First remove any existing mapping
        ExecutionEngine->updateGlobalMapping(F, nullptr);
        
        // Then add our new mapping
        ExecutionEngine->updateGlobalMapping(F, Addr);
            
        // Verify the mapping worked
        void* MappedAddr = ExecutionEngine->getPointerToGlobalIfAvailable(F);
        LOG(INFO) << "Verified mapping for " << name << ": " << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
        
        if (MappedAddr != Addr) {
            LOG(ERROR) << "Mapping verification failed. Expected: " << std::hex << reinterpret_cast<uintptr_t>(Addr)
                      << ", Got: " << std::hex << reinterpret_cast<uintptr_t>(MappedAddr);
        }
    } else {
        LOG(ERROR) << "Function " << name << " not found in module";
    }
}

bool JITEngine::ExecuteFunction(const std::string& name, void* state, uint64_t pc, void* memory) {
    if (!ExecutionEngine) {
        LOG(ERROR) << "Execution engine not initialized";
        return false;
    }

    LOG(INFO) << "Looking for function: " << name;

    // Check if function exists and get its address
    llvm::Function* F = ExecutionEngine->FindFunctionNamed(name);
    if (!F) {
        LOG(ERROR) << "Function " << name << " not found in module";
        return false;
    }

    LOG(INFO) << "Found function " << name << " in module";
    
    // Get the function address (object is already finalized)
    uint64_t FPtr = ExecutionEngine->getFunctionAddress(name);
    if (!FPtr) {
        LOG(ERROR) << "Failed to get address for function " << name;
        return false;
    }

    LOG(INFO) << "Function address: 0x" << std::hex << FPtr;
    
    // Cast to function type
    using FuncType = void* (*)(void*, uint64_t, void*);
    FuncType Func = (FuncType)FPtr;

    // debug breakpoint
    __asm__("int3");

    // Call the function
    Func(state, pc, memory);
    return true;
} 