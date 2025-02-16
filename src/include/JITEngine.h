#pragma once

#include <memory>
#include <string>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>

class JITEngine {
public:
    JITEngine();
    ~JITEngine();

    // Initialize the JIT engine
    bool Initialize();

    // Initialize the JIT engine with a module
    bool InitializeWithModule(std::unique_ptr<llvm::Module> module);

    // Add external function mapping by pointer (deprecated)
    void AddExternalMapping(llvm::Function* F, void* Addr);

    // Add external function mapping by name
    void AddExternalMappingByName(const std::string& name, void* Addr);

    // Execute a specific function from the module
    bool ExecuteFunction(const std::string& name, void* state, uint64_t pc, void* memory);

private:
    std::unique_ptr<llvm::ExecutionEngine> ExecutionEngine;
}; 