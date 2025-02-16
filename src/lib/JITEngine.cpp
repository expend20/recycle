#include "JITEngine.h"
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

    return true;
}

void JITEngine::AddExternalMapping(llvm::Function* F, void* Addr) {
    if (ExecutionEngine) {
        LOG(INFO) << "Mapping function " << F->getName().str() << " to address " << std::hex << reinterpret_cast<uintptr_t>(Addr);
        ExecutionEngine->addGlobalMapping(F, Addr);
    }
}

void JITEngine::AddExternalMappingByName(const std::string& name, void* Addr) {
    if (ExecutionEngine) {
        if (auto* F = ExecutionEngine->FindFunctionNamed(name)) {
            LOG(INFO) << "Mapping function " << name << " to address " << std::hex << reinterpret_cast<uintptr_t>(Addr);
            ExecutionEngine->addGlobalMapping(F, Addr);
        } else {
            LOG(ERROR) << "Failed to find function named " << name << " in module";
        }
    }
}

bool JITEngine::ExecuteFunction(const std::string& name, void* state, uint64_t pc, void* memory) {
    if (!ExecutionEngine) {
        LOG(ERROR) << "Execution engine not initialized";
        return false;
    }

    // Finalize object after all mappings are done but before getting function address
    ExecutionEngine->finalizeObject();

    // Get function address
    uint64_t FPtr = ExecutionEngine->getFunctionAddress(name);
    if (!FPtr) {
        LOG(ERROR) << "Function " << name << " not found in module";
        return false;
    }

    LOG(INFO) << "Executing function " << name << " at address 0x" << std::hex << FPtr;
    
    // Cast to function type
    using FuncType = void* (*)(void*, uint64_t, void*);
    FuncType Func = (FuncType)FPtr;

    // debug breakpoint
    __asm__("int3");

    // Call the function
    Func(state, pc, memory);
    return true;
} 