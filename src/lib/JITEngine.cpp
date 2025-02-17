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
    llvm::outs() << "Setting data layout\n";
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

    // Print the lifted LLVM IR
    std::error_code EC;
    llvm::raw_fd_ostream file("lifted.ll", EC);
    if (EC) {
        LOG(ERROR) << "Could not open file: " << EC.message();
        return false;
    }
    module->print(file, nullptr);
    file.close();
    llvm::outs() << "LLVM IR written to lifted.ll\n";

    // Create execution engine
    llvm::outs() << "Creating execution engine\n";
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
        {"__remill_missing_block", reinterpret_cast<void*>(Runtime::__remill_missing_block)},
        {"__remill_write_memory_64", reinterpret_cast<void*>(Runtime::__remill_write_memory_64)},
        {"sub_1400016d0", reinterpret_cast<void*>(Runtime::__sub_1400016d0)}
    };

    for (const auto& func : externalFuncs) {
        llvm::outs() << "Mapping " << func.name << "\n";
        llvm::Function* llvmFunc = modulePtr->getFunction(func.name);
        if (!llvmFunc) {
            LOG(ERROR) << "Failed to find " << func.name;
            return false;
        }
        llvm::outs() << "Function pointer: " << llvm::format_hex(reinterpret_cast<uintptr_t>(func.ptr), 16) << "\n";
        ExecutionEngine->addGlobalMapping(llvmFunc, func.ptr);
    }
    
    // Force symbol resolution
    ExecutionEngine->finalizeObject();
    
    // Verify all external functions are resolved
    for (auto &F : modulePtr->functions()) {
        if (F.isDeclaration()) {
            void *Addr = (void*)ExecutionEngine->getPointerToFunction(&F);
            llvm::outs() << "Function " << F.getName().data() 
                        << " resolved to address: " << llvm::format_hex(reinterpret_cast<uintptr_t>(Addr), 16) << "\n";
        }
    }

    return true;
}

void JITEngine::AddExternalMapping(llvm::Function* F, void* Addr) {
    if (ExecutionEngine) {
        ExecutionEngine->addGlobalMapping(F, Addr);
    }
}

void JITEngine::AddExternalMappingByName(const std::string& name, void* Addr) {
    if (ExecutionEngine) {
        if (auto* F = ExecutionEngine->FindFunctionNamed(name)) {
            ExecutionEngine->addGlobalMapping(F, Addr);
        }
    }
}

bool JITEngine::ExecuteFunction(const std::string& name, void* state, uint64_t pc, void* memory) {
    if (!ExecutionEngine) {
        LOG(ERROR) << "Execution engine not initialized";
        return false;
    }

    // find and call the function
    llvm::outs() << "Finding " << name << "\n";
    typedef void* (*FuncType)(void*, uint64_t, void*);
    FuncType Func = reinterpret_cast<FuncType>(ExecutionEngine->getFunctionAddress(name));
    if (!Func) {
        LOG(ERROR) << "Failed to find " << name;
        return false;
    }

    // Call the function
    Func(state, pc, memory);
    return true;
} 