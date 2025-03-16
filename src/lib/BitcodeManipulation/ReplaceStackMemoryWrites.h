#pragma once

#include <llvm/IR/Module.h>
#include <string>
#include <vector>

namespace BitcodeManipulation {

// Replace __remill_write_memory_* calls with GEP instructions
// when the memory address is calculated based on Stack variable
bool ReplaceStackMemoryWrites(
    llvm::Module& Module,
    const std::string& StackVariableName = "Stack",
    const std::vector<std::string>& MemWriteFunctions = {
        "__remill_write_memory_8", 
        "__remill_write_memory_16", 
        "__remill_write_memory_32",
        "__remill_write_memory_64"
    });

} // namespace BitcodeManipulation 