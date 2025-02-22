#pragma once

#include "Disasm/XEDDisassembler.h"

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
