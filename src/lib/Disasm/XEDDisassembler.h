#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "DecodedInstruction.h"

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
