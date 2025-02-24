#include "JITEngine.h"
#include "JITRuntime.h"
#include "BitcodeManipulation/MiscUtils.h"

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
        {"__rt_missing_block", reinterpret_cast<void*>(Runtime::__rt_missing_block)},

        {"__rt_read_memory64", reinterpret_cast<void*>(Runtime::__rt_read_memory64)},
        {"__rt_write_memory64", reinterpret_cast<void*>(Runtime::__rt_write_memory64)},
        {"__rt_read_memory32", reinterpret_cast<void*>(Runtime::__rt_read_memory32)},
        {"__rt_write_memory32", reinterpret_cast<void*>(Runtime::__rt_write_memory32)},
        {"__rt_read_memory16", reinterpret_cast<void*>(Runtime::__rt_read_memory16)},
        {"__rt_write_memory16", reinterpret_cast<void*>(Runtime::__rt_write_memory16)},
        {"__rt_read_memory8", reinterpret_cast<void*>(Runtime::__rt_read_memory8)},
        {"__rt_write_memory8", reinterpret_cast<void*>(Runtime::__rt_write_memory8)},

        {"__remill_async_hyper_call", reinterpret_cast<void*>(Runtime::__remill_async_hyper_call)},

        {"LogMessage", reinterpret_cast<void*>(Runtime::LogMessage)},
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

bool JITEngine::ExecuteFunction(const std::string& name) {
    if (!ExecutionEngine) {
        LOG(ERROR) << "Execution engine not initialized";
        return false;
    }

    // find and call the function
    typedef void (*FuncType)();
    FuncType Func = reinterpret_cast<FuncType>(ExecutionEngine->getFunctionAddress(name));
    if (!Func) {
        LOG(ERROR) << "Failed to find " << name;
        return false;
    }

    // Call the function
    VLOG(1) << "Calling " << name;
    Func();
    return true;
} 