#include "recycle.h"
#include <remill/OS/OS.h>
#include <remill/BC/Util.h>
#include <remill/BC/Version.h>
#include <remill/BC/InstructionLifter.h>
#include <remill/BC/Lifter.h>
#include <remill/BC/TraceLifter.h>
#include <remill/BC/Optimizer.h>
#include <iostream>
#include <glog/logging.h>
#include <llvm/Linker/Linker.h>

using Memory = std::map<uint64_t, uint8_t>;

class SimpleTraceManager : public remill::TraceManager
{
public:
    virtual ~SimpleTraceManager(void) = default;

    explicit SimpleTraceManager(Memory &memory_) : memory(memory_) {}

protected:
    // Called when we have lifted, i.e. defined the contents, of a new trace.
    // The derived class is expected to do something useful with this.
    void SetLiftedTraceDefinition(uint64_t addr,
                                  llvm::Function *lifted_func) override
    {
        traces[addr] = lifted_func;
    }

    // Get a declaration for a lifted trace. The idea here is that a derived
    // class might have additional global info available to them that lets
    // them declare traces ahead of time. In order to distinguish between
    // stuff we've lifted, and stuff we haven't lifted, we allow the lifter
    // to access "defined" vs. "declared" traces.
    //
    // NOTE: This is permitted to return a function from an arbitrary module.
    llvm::Function *GetLiftedTraceDeclaration(uint64_t addr) override
    {
        auto trace_it = traces.find(addr);
        if (trace_it != traces.end())
        {
            return trace_it->second;
        }
        else
        {
            return nullptr;
        }
    }

    // Get a definition for a lifted trace.
    //
    // NOTE: This is permitted to return a function from an arbitrary module.
    llvm::Function *GetLiftedTraceDefinition(uint64_t addr) override
    {
        return GetLiftedTraceDeclaration(addr);
    }

    // Try to read an executable byte of memory. Returns `true` of the byte
    // at address `addr` is executable and readable, and updates the byte
    // pointed to by `byte` with the read value.
    bool TryReadExecutableByte(uint64_t addr, uint8_t *byte) override
    {
        auto byte_it = memory.find(addr);
        if (byte_it != memory.end())
        {
            *byte = byte_it->second;
            return true;
        }
        else
        {
            return false;
        }
    }

public:
    Memory &memory;
    std::unordered_map<uint64_t, llvm::Function *> traces;
};

BasicBlockLifter::BasicBlockLifter() : context(std::make_unique<llvm::LLVMContext>()) {}

bool BasicBlockLifter::VerifyIntrinsics() {
    if (!intrinsics->error) {
        LOG(ERROR) << "Missing critical intrinsic: __remill_error";
        return false;
    }
    if (!intrinsics->jump) {
        LOG(ERROR) << "Missing critical intrinsic: __remill_jump";
        return false;
    }
    if (!intrinsics->function_call) {
        LOG(ERROR) << "Missing critical intrinsic: __remill_function_call";
        return false;
    }
    if (!intrinsics->function_return) {
        LOG(ERROR) << "Missing critical intrinsic: __remill_function_return";
        return false;
    }
    if (!intrinsics->missing_block) {
        LOG(ERROR) << "Missing critical intrinsic: __remill_missing_block";
        return false;
    }
    return true;
}

bool BasicBlockLifter::LiftBlock(
    const std::vector<DecodedInstruction>& instructions,
    uint64_t block_addr) {
    
    if (instructions.empty()) {
        LOG(WARNING) << "Empty instruction vector provided";
        return false;
    }

    // Initialize architecture if not already done
    if (!arch) {
        LOG(INFO) << "Creating architecture for windows/amd64";
        arch = remill::Arch::Get(*context, "windows", "amd64");
        if (!arch) {
            LOG(ERROR) << "Failed to create architecture";
            return false;
        }
    }

    // Create a map of bytes for the instructions
    std::map<uint64_t, uint8_t> memory;
    for (const auto& inst : instructions) {
        for (size_t i = 0; i < inst.bytes.size(); ++i) {
            memory[inst.address + i] = inst.bytes[i];
        }
    }

    // Load architecture semantics into module
    LOG(INFO) << "Loading architecture semantics";
    auto temp_module = remill::LoadArchSemantics(arch.get());
    if (!temp_module) {
        LOG(ERROR) << "Failed to create module";
        return false;
    }

    // Initialize intrinsics if not already done
    if (!intrinsics) {
        LOG(INFO) << "Initializing intrinsics table";
        intrinsics = std::make_unique<remill::IntrinsicTable>(temp_module.get());
        if (!VerifyIntrinsics()) {
            LOG(ERROR) << "Failed to verify critical intrinsics";
            return false;
        }
    }

    SimpleTraceManager inst_manager(memory);
    remill::TraceLifter inst_lifter(arch.get(), inst_manager);

    // Lift the trace starting at our block address
    LOG(INFO) << "Lifting trace at address 0x" << std::hex << block_addr;
    if (!inst_lifter.Lift(block_addr)) {
        LOG(ERROR) << "Failed to lift trace at address 0x" << std::hex << block_addr;
        return false;
    }

    // Optimize the lifted code
    remill::OptimizationGuide guide = {};
    remill::OptimizeModule(arch, temp_module, inst_manager.traces, guide);

    // Create destination module and prepare it
    dest_module = std::make_unique<llvm::Module>("lifted_code", *context);
    arch->PrepareModuleDataLayout(dest_module.get());

    // Move the lifted functions into the destination module
    for (auto &lifted_entry : inst_manager.traces) {
        remill::MoveFunctionIntoModule(lifted_entry.second, dest_module.get());
    }

    return true;
}