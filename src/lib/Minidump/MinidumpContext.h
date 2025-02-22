#pragma once

#include "Disasm/XEDDisassembler.h"
#include "third_party/udm_parser/src/lib/udmp-parser.h"

namespace MinidumpContext {

class MinidumpContext {
public:
    explicit MinidumpContext(const std::string& dump_path);
    bool Initialize();
    uint64_t GetInstructionPointer() const;
    uint64_t GetThreadTebAddress() const;
    std::vector<uint8_t> ReadMemory(uint64_t address, size_t size) const;

private:
    std::unique_ptr<udmpparser::UserDumpParser> parser;
    std::string dump_path;
};

}  // namespace MinidumpContext
