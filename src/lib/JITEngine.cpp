#include "JITEngine.h"
#include "JITRuntime.h"
#include "MiscUtils.h"
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

bool JITEngine::Initialize(std::unique_ptr<llvm::Module> module) {
    // Initialize LLVM's target infrastructure
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    if (!module) {
        return true;
    }

    VLOG(1) << "Setting data layout";
    std::unique_ptr<llvm::TargetMachine> TM(
        llvm::EngineBuilder().selectTarget(
            llvm::Triple(llvm::sys::getProcessTriple()),  // Use host triple
            "",
            llvm::sys::getHostCPUName(),
            llvm::SmallVector<std::string, 0>())
    );
    if (!TM) {
        LOG(ERROR) << "Failed to select target";
        return false;
    }
    module->setDataLayout(TM->createDataLayout());
    module->setTargetTriple(llvm::sys::getProcessTriple());

    // Create execution engine
    VLOG(1) << "Creating execution engine";
    std::string ErrStr;
    auto* modulePtr = module.get();  // Keep a copy of the raw pointer
    ExecutionEngine.reset(
        llvm::EngineBuilder(std::move(module))
        .setErrorStr(&ErrStr)
        .setEngineKind(llvm::EngineKind::JIT)
        .setMCPU(llvm::sys::getHostCPUName())
        .setOptLevel(llvm::CodeGenOpt::None)
        .create(TM.release())  // Release ownership of TM to ExecutionEngine
    );

    if (!ExecutionEngine) {
        LOG(ERROR) << "Failed to create execution engine: " << ErrStr;
        return false;
    }

    // Map external functions
    struct ExternalFunc {
        const char* name;
        void* ptr;
    };

    ExternalFunc externalFuncs[] = {
        {"__remill_log_function", reinterpret_cast<void*>(Runtime::__remill_log_function)},
        {"__remill_missing_block_final", reinterpret_cast<void*>(Runtime::__remill_missing_block_final)},
        {"__remill_write_memory_64", reinterpret_cast<void*>(Runtime::__remill_write_memory_64)},
        {"__remill_async_hyper_call", reinterpret_cast<void*>(Runtime::__remill_async_hyper_call)},
    };

    for (const auto& func : externalFuncs) {
        llvm::Function* llvmFunc = modulePtr->getFunction(func.name);
        if (llvmFunc) {
            ExecutionEngine->addGlobalMapping(llvmFunc, func.ptr);
        }
    }
    
    // Force symbol resolution
    ExecutionEngine->finalizeObject();

    return true;
}

bool JITEngine::ExecuteFunction(const std::string& name, void* state, uint64_t pc, void* memory) {
    if (!ExecutionEngine) {
        LOG(ERROR) << "Execution engine not initialized";
        return false;
    }

    // find and call the function
    typedef void* (*FuncType)(void*, uint64_t, void*);
    FuncType Func = reinterpret_cast<FuncType>(ExecutionEngine->getFunctionAddress(name));
    if (!Func) {
        LOG(ERROR) << "Failed to find " << name;
        return false;
    }

    // Call the function
    VLOG(1) << "Calling " << name;
    Func(state, pc, memory);
    return true;
} 