#include "recycle.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <iomanip>

BasicBlockDisassembler::BasicBlockDisassembler(size_t max_inst)
    : max_instructions(max_inst) {}

std::vector<DecodedInstruction> BasicBlockDisassembler::DisassembleBlock(
    const uint8_t* memory, size_t size, uint64_t start_addr) {
    
    std::vector<DecodedInstruction> instructions;
    const uint8_t* current_bytes = memory;
    size_t remaining = size;
    uint64_t current_addr = start_addr;

    llvm::outs() << "\nDisassembling block at "
                 << llvm::format_hex_no_prefix(start_addr, 16) << ":\n";
    llvm::outs() << "----------------------------------------\n";

    while (remaining > 0 && instructions.size() < max_instructions) {
        auto inst = disasm.DecodeInstruction(current_bytes, remaining, current_addr);
        if (inst.length == 0) {
            break;
        }

        // Log the instruction
        llvm::outs() << llvm::format_hex_no_prefix(inst.address, 16) << ": ";
        
        // Print bytes without 0x prefix
        for (const auto& byte : inst.bytes) {
            llvm::outs() << llvm::format_hex_no_prefix(static_cast<uint32_t>(byte), 2) << " ";
        }
        
        // Pad with spaces for alignment (assuming max 15 bytes per instruction)
        for (size_t i = inst.bytes.size(); i < 15; ++i) {
            llvm::outs() << "   ";
        }
        
        // Print instruction type
        if (inst.is_branch) llvm::outs() << "[branch] ";
        else if (inst.is_call) llvm::outs() << "[call] ";
        else if (inst.is_ret) llvm::outs() << "[ret] ";
        else llvm::outs() << "        ";
        
        // Print assembly with proper padding
        if (!inst.assembly.empty()) {
            llvm::outs() << inst.assembly;
        }
        llvm::outs() << "\n";

        instructions.push_back(inst);
        
        if (disasm.IsTerminator(inst)) {
            llvm::outs() << "Block terminated by ";
            if (inst.is_branch) llvm::outs() << "branch instruction\n";
            else if (inst.is_call) llvm::outs() << "call instruction\n";
            else if (inst.is_ret) llvm::outs() << "return instruction\n";
            else llvm::outs() << "terminator instruction\n";
            break;
        }

        current_bytes += inst.length;
        current_addr += inst.length;
        remaining -= inst.length;
    }

    llvm::outs() << "----------------------------------------\n";
    llvm::outs() << "Total instructions: " << instructions.size() << "\n\n";

    return instructions;
} 