#include <remill/Arch/Arch.h>
#include <remill/Arch/Instruction.h>
#include <remill/Arch/Name.h>
#include <remill/BC/Version.h>
#include <remill/BC/Util.h>
#include <remill/OS/OS.h>
#include <remill/BC/IntrinsicTable.h>
#include <remill/BC/Lifter.h>
#include <remill/BC/Util.h>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

DEFINE_string(arch, "amd64", "Architecture to use");
DEFINE_string(os, "linux", "Operating system to use");

int main(int argc, char *argv[]) {
    // Initialize gflags and glog
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_v = 2;  // Increase verbosity for debugging

    // Initialize LLVM targets
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    LOG(INFO) << "Initializing LLVM context and module";
    
    // Initialize LLVM context and module
    auto context = std::make_unique<llvm::LLVMContext>();
    if (!context) {
        LOG(ERROR) << "Failed to create LLVM context";
        return 1;
    }

    std::unique_ptr<llvm::Module> module = std::make_unique<llvm::Module>("example", *context);
    if (!module) {
        LOG(ERROR) << "Failed to create LLVM module";
        return 1;
    }

    LOG(INFO) << "Creating architecture object";

    // Create an architecture object for x86-64
    auto arch = remill::Arch::Get(*context, remill::kOSLinux, remill::kArchAMD64);
    if (!arch) {
        LOG(ERROR) << "Failed to create architecture object";
        return 1;
    }

    LOG(INFO) << "Architecture object created successfully";

    // Load architecture semantics
    LOG(INFO) << "Loading architecture semantics";
    auto semantics_module = remill::LoadArchSemantics(arch.get());
    if (!semantics_module) {
        LOG(ERROR) << "Failed to load architecture semantics";
        return 1;
    }

    // Initialize the module with Remill's basic blocks
    LOG(INFO) << "Initializing module";
    arch->PrepareModule(module.get());
    arch->InitFromSemanticsModule(semantics_module.get());

    // Clone the state structure and intrinsics from semantics module
    LOG(INFO) << "Cloning state and intrinsics";
    
    // Clone state structure
    if (auto *state_global = semantics_module->getGlobalVariable("__remill_state")) {
        if (auto *state_type = llvm::dyn_cast<llvm::StructType>(state_global->getValueType())) {
            LOG(INFO) << "Cloning state type";
            auto *new_state_type = llvm::StructType::create(module->getContext(), state_type->elements(), "State", state_type->isPacked());
            module->getOrInsertGlobal("__remill_state", new_state_type);
            LOG(INFO) << "Successfully cloned state type";
        } else {
            LOG(ERROR) << "Failed to get state type from __remill_state global";
            return 1;
        }
    } else {
        LOG(ERROR) << "Failed to find __remill_state global in semantics module";
        return 1;
    }

    // Clone the intrinsics
    LOG(INFO) << "Cloning intrinsics";
    const char *intrinsics[] = {
        "__remill_error",
        "__remill_jump",
        "__remill_function_call",
        "__remill_function_return",
        "__remill_missing_block",
        "__remill_sync_hyper_call",
        "__remill_async_hyper_call",
        "__remill_read_memory_8",
        "__remill_read_memory_16",
        "__remill_read_memory_32",
        "__remill_read_memory_64",
        "__remill_write_memory_8",
        "__remill_write_memory_16",
        "__remill_write_memory_32",
        "__remill_write_memory_64",
        "__remill_read_memory_f32",
        "__remill_read_memory_f64",
        "__remill_read_memory_f80",
        "__remill_read_memory_f128",
        "__remill_write_memory_f32",
        "__remill_write_memory_f64",
        "__remill_write_memory_f80",
        "__remill_write_memory_f128",
        "__remill_barrier_load_load",
        "__remill_barrier_load_store",
        "__remill_barrier_store_load",
        "__remill_barrier_store_store",
        "__remill_atomic_begin",
        "__remill_atomic_end",
        "__remill_delay_slot_begin",
        "__remill_delay_slot_end",
        "__remill_undefined_8",
        "__remill_undefined_16",
        "__remill_undefined_32",
        "__remill_undefined_64",
        "__remill_undefined_f32",
        "__remill_undefined_f64",
        "__remill_undefined_f80",
        "__remill_flag_computation_zero",
        "__remill_flag_computation_sign",
        "__remill_flag_computation_overflow",
        "__remill_flag_computation_carry",
        "__remill_compare_eq",
        "__remill_compare_neq",
        "__remill_compare_slt",
        "__remill_compare_sle",
        "__remill_compare_sgt",
        "__remill_compare_sge",
        "__remill_compare_ult",
        "__remill_compare_ule",
        "__remill_compare_ugt",
        "__remill_compare_uge",
        "INVALID_INSTRUCTION",
        "__remill_basic_block"
    };

    // First create the __remill_intrinsics function
    LOG(INFO) << "Creating __remill_intrinsics function";
    auto void_type = llvm::Type::getVoidTy(*context);
    auto func_type = llvm::FunctionType::get(void_type, false);
    auto intrinsics_func = llvm::Function::Create(func_type,
                                                 llvm::GlobalValue::ExternalLinkage,
                                                 "__remill_intrinsics", module.get());
    auto block = llvm::BasicBlock::Create(*context, "entry", intrinsics_func);
    llvm::ReturnInst::Create(*context, block);
    LOG(INFO) << "Successfully created __remill_intrinsics function";

    // Then clone the other intrinsics
    for (const char *name : intrinsics) {
        if (auto *func = semantics_module->getFunction(name)) {
            LOG(INFO) << "Cloning intrinsic: " << name;
            auto *new_func = llvm::Function::Create(func->getFunctionType(),
                                                  llvm::GlobalValue::ExternalLinkage,
                                                  name, module.get());
            remill::CloneFunctionInto(func, new_func);
            LOG(INFO) << "Successfully cloned intrinsic: " << name;
        } else {
            LOG(WARNING) << "Failed to find intrinsic: " << name;
            // Create INVALID_INSTRUCTION if it doesn't exist
            if (std::string_view(name) == "INVALID_INSTRUCTION") {
                LOG(INFO) << "Creating INVALID_INSTRUCTION function";
                // Get the type from __remill_error since it has the same signature
                if (auto *error_func = semantics_module->getFunction("__remill_error")) {
                    auto *new_func = llvm::Function::Create(error_func->getFunctionType(),
                                                          llvm::GlobalValue::ExternalLinkage,
                                                          name, module.get());
                    auto *entry = llvm::BasicBlock::Create(*context, "entry", new_func);
                    llvm::IRBuilder<> builder(entry);
                    // Call __remill_error with the same arguments
                    auto *error_call = builder.CreateCall(error_func, {new_func->getArg(0), new_func->getArg(1), new_func->getArg(2)});
                    builder.CreateRet(error_call);
                    LOG(INFO) << "Successfully created INVALID_INSTRUCTION function";
                }
            }
        }
    }
    LOG(INFO) << "Finished cloning intrinsics";

    // Create intrinsics table
    LOG(INFO) << "Creating intrinsics table";
    auto intrinsics_table = std::make_unique<remill::IntrinsicTable>(module.get());
    if (!intrinsics_table) {
        LOG(ERROR) << "Failed to create intrinsics table";
        return 1;
    }

    // Create instruction lifter
    LOG(INFO) << "Creating instruction lifter";
    auto lifter = std::make_unique<remill::InstructionLifter>(arch.get(), intrinsics_table.get());
    if (!lifter) {
        LOG(ERROR) << "Failed to create instruction lifter";
        return 1;
    }

    // Example instruction bytes (NOP instruction)
    std::vector<uint8_t> bytes{0x90}; // NOP instruction
    remill::Instruction inst;
    uint64_t address = 0x1000;

    LOG(INFO) << "Creating decoding context";

    // Create a default decoding context
    remill::DecodingContext dec_context;

    LOG(INFO) << "Attempting to decode instruction at address " << std::hex << address;

    // Decode the instruction
    std::string_view bytes_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!arch->DecodeInstruction(address, bytes_view, inst, dec_context)) {
        LOG(ERROR) << "Failed to decode instruction at address " << std::hex << address;
        return 1;
    }

    LOG(INFO) << "Successfully decoded instruction";

    // Print the lifted instruction
    std::cout << "Lifted Instruction: " << inst.Serialize() << std::endl;

    return 0;
}