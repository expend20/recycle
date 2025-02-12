#include <iostream>
#include <string>
#include <iomanip>
#include "third_party/udm_parser/src/lib/udmp-parser.h"
extern "C" {
#define XED_DLL
#include <xed/xed-interface.h>
}  // extern C

void init_xed() {
    xed_tables_init();
}

std::string disassemble_instruction(const uint8_t* bytes, size_t size, uint64_t addr) {
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero(&xedd);
    xed_decoded_inst_set_mode(&xedd, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
    
    xed_error_enum_t error = xed_decode(&xedd, bytes, size);
    if (error != XED_ERROR_NONE) {
        return "Failed to decode instruction";
    }

    char buffer[256];
    xed_format_context(XED_SYNTAX_INTEL, &xedd, buffer, sizeof(buffer), addr, nullptr, nullptr);
    return std::string(buffer);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_minidump>\n";
        return 1;
    }

    // Initialize XED
    init_xed();

    const char* minidump_path = argv[1];
    udmpparser::UserDumpParser parser;

    // Try to parse the minidump file
    if (!parser.Parse(minidump_path)) {
        std::cerr << "Failed to parse minidump file: " << minidump_path << "\n";
        return 1;
    }

    // Get the foreground thread ID (usually the one that crashed)
    auto foreground_thread_id = parser.GetForegroundThreadId();
    if (!foreground_thread_id) {
        std::cerr << "Could not find foreground thread ID\n";
        return 1;
    }

    // Get the thread information
    const auto& threads = parser.GetThreads();
    auto thread_it = threads.find(*foreground_thread_id);
    if (thread_it == threads.end()) {
        std::cerr << "Could not find thread with ID: " << *foreground_thread_id << "\n";
        return 1;
    }

    // Get the thread context
    const auto& thread = thread_it->second;
    const auto& context = thread.Context;

    // Check if it's a 64-bit context and get RIP
    uint64_t rip;
    if (std::holds_alternative<udmpparser::Context64_t>(context)) {
        const auto& ctx64 = std::get<udmpparser::Context64_t>(context);
        rip = ctx64.Rip;
        std::cout << "RIP: 0x" << std::hex << rip << std::dec << "\n";
    } else {
        std::cerr << "Not a 64-bit context\n";
        return 1;
    }

    // Read memory at RIP
    auto memory = parser.ReadMemory(rip, 256); // Read 15 bytes (enough for a few instructions)
    if (!memory) {
        std::cerr << "Failed to read memory at RIP\n";
        return 1;
    }

    // Disassemble first few instructions
    const uint8_t* code = memory->data();
    size_t remaining = memory->size();
    uint64_t current_addr = rip;
    
    std::cout << "\nDisassembly:\n";
    for (int i = 0; i < 5 && remaining > 0; i++) { // Try to disassemble up to 5 instructions
        std::string disasm = disassemble_instruction(code, remaining, current_addr);
        std::cout << std::hex << std::setw(16) << std::setfill('0') << current_addr << ": " 
                  << disasm << "\n";

        // Move to next instruction
        xed_decoded_inst_t xedd;
        xed_decoded_inst_zero(&xedd);
        xed_decoded_inst_set_mode(&xedd, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
        if (XED_ERROR_NONE != xed_decode(&xedd, code, remaining)) {
            break;
        }
        
        unsigned int length = xed_decoded_inst_get_length(&xedd);
        code += length;
        remaining -= length;
        current_addr += length;
    }

    return 0;
}
