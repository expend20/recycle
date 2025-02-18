#include "recycle.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <glog/logging.h>
#include <iomanip>
#include <sstream>

BasicBlockDisassembler::BasicBlockDisassembler(size_t max_inst)
    : max_instructions(max_inst) {}

std::vector<DecodedInstruction> BasicBlockDisassembler::DisassembleBlock(
    const uint8_t* memory, size_t size, uint64_t start_addr) {
    
    std::vector<DecodedInstruction> instructions;
    const uint8_t* current_bytes = memory;
    size_t remaining = size;
    uint64_t current_addr = start_addr;

    std::string addr_str;
    llvm::raw_string_ostream rso(addr_str);
    rso << llvm::format_hex_no_prefix(start_addr, 16);
    
    VLOG(1) << "Disassembling block at " << addr_str << ":";
    VLOG(1) << "----------------------------------------";

    while (remaining > 0 && instructions.size() < max_instructions) {
        auto inst = disasm.DecodeInstruction(current_bytes, remaining, current_addr);
        if (inst.length == 0) {
            break;
        }

        // Build the log message using stringstream for better formatting
        std::stringstream ss;
        addr_str.clear();
        rso << llvm::format_hex_no_prefix(inst.address, 16);
        ss << addr_str << ": ";
        
        // Print bytes without 0x prefix
        for (const auto& byte : inst.bytes) {
            std::string byte_str;
            llvm::raw_string_ostream byte_rso(byte_str);
            byte_rso << llvm::format_hex_no_prefix(static_cast<uint32_t>(byte), 2);
            ss << byte_str;
        }
        
        // Pad with spaces for alignment (assuming max 15 bytes per instruction)
        for (size_t i = inst.bytes.size(); i < 10; ++i) {
            ss << "  ";
        }
        
        // Print instruction type
        if (inst.is_branch) ss << "[branch] ";
        else if (inst.is_call) ss << "[call] ";
        else if (inst.is_ret) ss << "[ret] ";
        else if (inst.is_int3) ss << "[int3] ";
        else ss << "        ";
        
        // Print assembly with proper padding
        if (!inst.assembly.empty()) {
            ss << inst.assembly;
        }
        LOG(INFO) << ss.str();

        instructions.push_back(inst);
        
        if (disasm.IsTerminator(inst)) {
            std::string terminator_type;
            if (inst.is_branch) terminator_type = "branch instruction";
            else if (inst.is_call) terminator_type = "call instruction";
            else if (inst.is_ret) terminator_type = "return instruction";
            else if (inst.is_int3) terminator_type = "int3 instruction";
            else terminator_type = "terminator instruction";
            VLOG(1) << "Block terminated by " << terminator_type;
            break;
        }

        current_bytes += inst.length;
        current_addr += inst.length;
        remaining -= inst.length;
    }

    VLOG(1) << "----------------------------------------";
    VLOG(1) << "Total instructions: " << instructions.size() << "\n";

    return instructions;
} 