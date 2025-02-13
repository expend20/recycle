#include "recycle.h"
#include "third_party/udm_parser/src/lib/udmp-parser.h"
#include <glog/logging.h>

MinidumpContext::MinidumpContext(const std::string& dump_path_)
    : parser(std::make_unique<udmpparser::UserDumpParser>())
    , dump_path(dump_path_) {}

bool MinidumpContext::Initialize() {
    LOG(INFO) << "Initializing minidump parser for file: " << dump_path;
    if (!parser->Parse(dump_path.c_str())) {
        LOG(ERROR) << "Failed to parse minidump file: " << dump_path;
        return false;
    }

    auto foreground_thread_id = parser->GetForegroundThreadId();
    if (!foreground_thread_id) {
        LOG(ERROR) << "Could not find foreground thread ID";
        return false;
    }

    const auto& threads = parser->GetThreads();
    auto thread_it = threads.find(*foreground_thread_id);
    if (thread_it == threads.end()) {
        LOG(ERROR) << "Could not find thread with ID: " << *foreground_thread_id;
        return false;
    }

    LOG(INFO) << "Successfully initialized minidump parser";
    return true;
}

uint64_t MinidumpContext::GetInstructionPointer() const {
    auto foreground_thread_id = parser->GetForegroundThreadId();
    const auto& threads = parser->GetThreads();
    auto thread_it = threads.find(*foreground_thread_id);
    const auto& thread = thread_it->second;
    const auto& context = thread.Context;

    if (std::holds_alternative<udmpparser::Context64_t>(context)) {
        const auto& ctx64 = std::get<udmpparser::Context64_t>(context);
        LOG(INFO) << "Found instruction pointer at 0x" << std::hex << ctx64.Rip;
        return ctx64.Rip;
    }

    LOG(WARNING) << "Could not find 64-bit context, returning 0";
    return 0;
}

std::vector<uint8_t> MinidumpContext::ReadMemoryAtIP(size_t size) const {
    uint64_t ip = GetInstructionPointer();
    auto memory = parser->ReadMemory(ip, size);
    if (!memory) {
        LOG(ERROR) << "Failed to read memory at IP: 0x" << std::hex << ip;
        return {};
    }
    LOG(INFO) << "Memory read at IP: 0x" << std::hex << ip << " size: " << std::dec << memory->size();
    return *memory;
} 