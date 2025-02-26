#include "MinidumpContext.h"

#include <glog/logging.h>

namespace MinidumpContext {

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

    VLOG(1) << "Successfully initialized minidump parser";
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
        VLOG(1) << "Found instruction pointer at 0x" << std::hex << ctx64.Rip;
        return ctx64.Rip;
    }

    LOG(WARNING) << "Could not find 64-bit context, returning 0";
    return 0;
}


std::vector<uint8_t> MinidumpContext::ReadMemory(uint64_t address, size_t size) const {
    auto memory = parser->ReadMemory(address, size);
    if (!memory) {
        LOG(ERROR) << "Failed to read memory at address: 0x" << std::hex << address;
        exit(1);
        return {};
    }
    VLOG(1) << "Memory read at address: 0x" << std::hex << address << " size: " << std::dec << memory->size();
    return *memory;
}

uint64_t MinidumpContext::GetThreadTebAddress() const {
    auto foreground_thread_id = parser->GetForegroundThreadId();
    const auto& threads = parser->GetThreads();
    auto thread_it = threads.find(*foreground_thread_id);
    const auto& thread = thread_it->second;
    VLOG(1) << "Found TEB address at 0x" << std::hex << thread.Teb;
    return thread.Teb;
} 

}  // namespace MinidumpContext