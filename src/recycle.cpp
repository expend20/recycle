#include "recycle.h"
#include <iostream>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>
#include <glog/logging.h>

int main(int argc, char* argv[]) {
    // Initialize Google logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;  // Ensure logs go to stderr

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_minidump>\n";
        return 1;
    }

    // Initialize minidump context
    MinidumpContext context(argv[1]);
    if (!context.Initialize()) {
        std::cerr << "Failed to initialize minidump context\n";
        return 1;
    }

    // Get instruction pointer and read memory
    uint64_t ip = context.GetInstructionPointer();
    llvm::outs() << "Starting disassembly from instruction pointer: 0x" 
                 << llvm::format_hex(ip, 16) << "\n";

    auto memory = context.ReadMemoryAtIP(256); // Read enough for a basic block
    if (memory.empty()) {
        std::cerr << "Failed to read memory at IP\n";
        return 1;
    }

    llvm::outs() << "Successfully read " << memory.size() 
                 << " bytes of memory for disassembly\n";

    // Disassemble the basic block
    BasicBlockDisassembler disassembler;
    auto instructions = disassembler.DisassembleBlock(memory.data(), memory.size(), ip);

    if (instructions.empty()) {
        std::cerr << "No instructions decoded\n";
        return 1;
    }

    // Initialize lifter and lift the block
    BasicBlockLifter lifter;
    if (!lifter.LiftBlock(instructions, ip)) {
        std::cerr << "Failed to lift basic block\n";
        return 1;
    }

    // Print the lifted LLVM IR
    std::error_code EC;
    llvm::raw_fd_ostream file("lifted.ll", EC);
    if (EC) {
        std::cerr << "Could not open file: " << EC.message() << "\n";
        return 1;
    }
    lifter.GetModule()->print(file, nullptr);
    file.close();
    
    llvm::outs() << "LLVM IR written to lifted.ll\n";

    return 0;
} 