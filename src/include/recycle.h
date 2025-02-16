#pragma once

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <remill/Arch/Arch.h>
#include <remill/BC/IntrinsicTable.h>
#include <remill/BC/Lifter.h>
#include "third_party/udm_parser/src/lib/udmp-parser.h"

namespace udmpparser {
class UserDumpParser;
}

// Forward declarations
class MinidumpContext;
class BasicBlockLifter;
class XEDDisassembler;

// Structure to hold decoded instruction information
struct DecodedInstruction {
    std::vector<uint8_t> bytes;
    size_t length;
    uint64_t address;
    bool is_branch;
    bool is_call;
    bool is_ret;
    std::string assembly;  // Assembly text representation
};

// Class to handle minidump context extraction
class MinidumpContext {
public:
    explicit MinidumpContext(const std::string& dump_path);
    bool Initialize();
    uint64_t GetInstructionPointer() const;
    std::vector<uint8_t> ReadMemoryAtIP(size_t size) const;

private:
    std::unique_ptr<udmpparser::UserDumpParser> parser;
    std::string dump_path;
};

// Class to handle disassembly using XED
class XEDDisassembler {
public:
    XEDDisassembler();
    ~XEDDisassembler();

    DecodedInstruction DecodeInstruction(const uint8_t* bytes, size_t max_size, uint64_t addr);
    bool IsTerminator(const DecodedInstruction& inst) const;

private:
    void Initialize();
};

// Class to handle basic block disassembly
class BasicBlockDisassembler {
public:
    BasicBlockDisassembler(size_t max_inst = 32);
    
    std::vector<DecodedInstruction> DisassembleBlock(const uint8_t* memory, 
                                                    size_t size, 
                                                    uint64_t start_addr);

private:
    XEDDisassembler disasm;
    size_t max_instructions;
};

// Class to handle lifting to LLVM IR using Remill
class BasicBlockLifter {
public:
    BasicBlockLifter();
    
    bool LiftBlock(const std::vector<DecodedInstruction>& instructions,
                   uint64_t block_addr);
    
    llvm::Module* GetModule() { return dest_module.get(); }
    std::unique_ptr<llvm::Module> TakeModule() { return std::move(dest_module); }

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> dest_module;
    remill::Arch::ArchPtr arch;
    std::unique_ptr<remill::IntrinsicTable> intrinsics;
    
    bool VerifyIntrinsics();
}; 