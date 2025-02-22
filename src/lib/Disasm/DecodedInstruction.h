#pragma once

#include <vector>
#include <cstdint>
#include <string>

// Structure to hold decoded instruction information
struct DecodedInstruction {
    std::vector<uint8_t> bytes;
    size_t length;
    uint64_t address;
    bool is_branch;
    bool is_call;
    bool is_ret;
    bool is_int3;
    std::string assembly;  // Assembly text representation
};
