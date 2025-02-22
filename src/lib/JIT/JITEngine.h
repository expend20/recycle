#pragma once

#include <memory>
#include <string>

#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>

class JITEngine {
public:
    JITEngine();
    ~JITEngine();

    // Initialize the JIT engine, optionally with a module
    bool Initialize(std::unique_ptr<llvm::Module> module = nullptr);

    // Execute a specific function from the module
    bool ExecuteFunction(const std::string& name);

private:
    std::unique_ptr<llvm::ExecutionEngine> ExecutionEngine;
}; 