#include "recycle.h"
#include <remill/OS/OS.h>
#include <remill/BC/Util.h>
#include <remill/BC/Version.h>
#include <remill/BC/InstructionLifter.h>
#include <remill/BC/Lifter.h>
#include <remill/BC/TraceLifter.h>
#include <iostream>
#include <glog/logging.h>

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

BasicBlockLifter::BasicBlockLifter() {
    InitializeLifter();
}

bool BasicBlockLifter::InitializeLifter() {
    LOG(INFO) << "Initializing BasicBlockLifter...";
    
    // Create LLVM context and module
    context = std::make_unique<llvm::LLVMContext>();
    LOG(INFO) << "Created LLVM context";

    // Initialize architecture (assuming x86-64 for now)
    LOG(INFO) << "Creating architecture for windows/amd64";
    arch = remill::Arch::Get(*context, "windows", "amd64");
    if (!arch) {
        LOG(ERROR) << "Failed to create architecture";
        return false;
    }

    // Load architecture semantics into module
    LOG(INFO) << "Loading architecture semantics";
    module = remill::LoadArchSemantics(arch.get());
    if (!module) {
        LOG(ERROR) << "Failed to load architecture semantics";
        return false;
    }
    LOG(INFO) << "Created module with architecture semantics";
    
    // Initialize intrinsics
    LOG(INFO) << "Initializing intrinsics table";
    intrinsics = std::make_unique<remill::IntrinsicTable>(module.get());
    
    // Verify critical intrinsics
    if (!VerifyIntrinsics()) {
        LOG(ERROR) << "Failed to verify critical intrinsics";
        return false;
    }
    
    LOG(INFO) << "BasicBlockLifter initialization complete";
    return true;
}

bool BasicBlockLifter::VerifyIntrinsics() {
    // Check if error intrinsic exists
    if (!intrinsics->error) {
        LOG(ERROR) << "Missing critical intrinsic: __remill_error";
        return false;
    }
    LOG(INFO) << "Found intrinsic: __remill_error";

    // Check other critical intrinsics
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

    LOG(INFO) << "All critical intrinsics verified";
    return true;
}

bool BasicBlockLifter::LiftBlock(
    const std::vector<DecodedInstruction>& instructions,
    uint64_t block_addr) {
    
    LOG(INFO) << "Lifting block at address 0x" << std::hex << block_addr;
    
    if (instructions.empty()) {
        LOG(WARNING) << "Empty instruction vector provided";
        return false;
    }

    // Create a new module for trace lifting
    LOG(INFO) << "Creating new module for trace lifting";
    auto trace_module = remill::LoadArchSemantics(arch.get());
    if (!trace_module) {
        LOG(ERROR) << "Failed to create trace module";
        return false;
    }

    // Create a map of bytes for the instructions
    std::map<uint64_t, uint8_t> memory;
    for (const auto& inst : instructions) {
        for (size_t i = 0; i < inst.bytes.size(); ++i) {
            memory[inst.address + i] = inst.bytes[i];
        }
    }

    SimpleTraceManager inst_manager(memory);

    // Create trace manager with our memory
    LOG(INFO) << "Creating trace manager and lifter";
    remill::TraceLifter inst_lifter(arch.get(), inst_manager);

    // Lift the trace starting at our block address
    LOG(INFO) << "Lifting trace at address 0x" << std::hex << block_addr;
    if (!inst_lifter.Lift(block_addr)) {
        LOG(ERROR) << "Failed to lift trace at address 0x" << std::hex << block_addr;
        return false;
    }

    // Get the lifted function
    auto func = inst_manager.traces[block_addr];
    if (!func) {
        LOG(ERROR) << "Could not find lifted trace for address 0x" << std::hex << block_addr;
        return false;
    }

    LOG(INFO) << "Moving lifted function into target module";
    LOG(INFO) << "Source module: " << trace_module.get() << ", Target module: " << module.get();
    //remill::MoveFunctionIntoModule(func, module.get());

    return true;
}

bool BasicBlockLifter::LiftInstruction(
    const DecodedInstruction& inst,
    llvm::BasicBlock* block,
    llvm::Value* state_ptr) {

    LOG(INFO) << "Lifting instruction at 0x" << std::hex << inst.address;

    remill::Instruction remill_inst;
    remill_inst.pc = inst.address;
    remill_inst.next_pc = inst.address + inst.length;
    remill_inst.bytes.assign(inst.bytes.begin(), inst.bytes.end());

    auto lifter = std::make_unique<remill::InstructionLifter>(arch.get(), intrinsics.get());
    auto status = lifter->LiftIntoBlock(remill_inst, block, state_ptr);

    if (status != remill::LiftStatus::kLiftedInstruction) {
        LOG(ERROR) << "Failed to lift instruction at 0x" << std::hex << inst.address 
                  << " status: " << static_cast<int>(status);
        return false;
    }

    return true;
} 