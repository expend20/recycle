#include "MiscUtils.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <glog/logging.h>
#include <set>
#include "remill/Arch/X86/Runtime/State.h"

namespace MiscUtils {

// just clone the module
std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& M) {
    return llvm::CloneModule(M);
}

// just as a sample
void MergeModules(llvm::Module& M1, const llvm::Module& M2) {

    VLOG(1) << "Merging modules";

    VLOG(1) << "Verifying M2";
    if (llvm::verifyModule(M2, &llvm::errs())) {
        LOG(ERROR) << "M2 is not valid";
        return;
    }
    VLOG(1) << "Verifying M1";    
    if (llvm::verifyModule(M1, &llvm::errs())) {
        LOG(ERROR) << "M1 is not valid";
        return;
    }
    // Clone M2 to avoid modifying the original
    auto ClonedM2 = llvm::CloneModule(M2);
    VLOG(1) << "Cloned M2";
    
    // Link the modules
    llvm::Linker::linkModules(M1, std::move(ClonedM2));
    VLOG(1) << "Linked modules";
}

void DumpModule(const llvm::Module& M, const std::string& filename) {
    std::error_code EC;
    llvm::raw_fd_ostream file(filename, EC);
    if (EC) {
        LOG(ERROR) << "Could not open file: " << EC.message();
        return;
    }
    M.print(file, nullptr);
    file.close();
    LOG(INFO) << "LLVM IR written to " << filename;
}

void AddMissingBlockHandler(llvm::Module& M, 
    const std::vector<std::pair<uint64_t, std::string>>& addr_to_func) {
    
    auto& context = M.getContext();
    
    // Get or create the function type for missing block handler
    auto voidPtrTy = llvm::Type::getInt8PtrTy(context);
    auto int64Ty = llvm::Type::getInt64Ty(context);
    
    std::vector<llvm::Type*> argTypes = {voidPtrTy, int64Ty, voidPtrTy};
    auto funcTy = llvm::FunctionType::get(voidPtrTy, argTypes, false);
    
    // Get or create the missing block handler function
    auto missingBlockFunc = M.getOrInsertFunction("__remill_missing_block", funcTy);
    auto func = llvm::cast<llvm::Function>(missingBlockFunc.getCallee());

    // If the function already exists and has a body, we need to preserve existing cases
    std::set<uint64_t> existingAddresses;
    llvm::SwitchInst* existingSwitch = nullptr;
    llvm::BasicBlock* defaultBB = nullptr;

    if (!func->empty()) {
        // Find the existing switch instruction and collect existing cases
        for (auto& BB : *func) {
            for (auto& I : BB) {
                if (auto* switchInst = llvm::dyn_cast<llvm::SwitchInst>(&I)) {
                    existingSwitch = switchInst;
                    defaultBB = switchInst->getDefaultDest();
                    
                    // Collect existing addresses
                    for (auto Case : switchInst->cases()) {
                        if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(Case.getCaseValue())) {
                            existingAddresses.insert(constInt->getZExtValue());
                        }
                    }
                    break;
                }
            }
            if (existingSwitch) break;
        }
    }

    // If no existing function body, create new entry block and switch
    if (func->empty()) {
        auto entryBB = llvm::BasicBlock::Create(context, "entry", func);
        llvm::IRBuilder<> builder(entryBB);
        
        // Get function arguments
        auto args = func->arg_begin();
        auto state = args++;
        auto pc = args++;
        auto memory = args;
        
        // Create default block and switch instruction
        defaultBB = llvm::BasicBlock::Create(context, "default", func);
        auto switchInst = builder.CreateSwitch(pc, defaultBB);
        existingSwitch = switchInst;

        // Create the default block with call to __remill_missing_block_final
        llvm::IRBuilder<> defaultBuilder(defaultBB);
        auto finalFunc = M.getOrInsertFunction("__remill_missing_block_final", funcTy);
        auto call = defaultBuilder.CreateCall(finalFunc, {state, pc, memory});
        defaultBuilder.CreateRet(call);
    }

    // Add new cases for addresses that don't exist yet
    for (const auto& mapping : addr_to_func) {
        // Skip if this address already has a case
        if (existingAddresses.count(mapping.first) > 0) {
            VLOG(1) << "Skipping existing case for address 0x" << std::hex << mapping.first;
            continue;
        }

        auto caseBB = llvm::BasicBlock::Create(context, "case_" + std::to_string(mapping.first), func);
        llvm::IRBuilder<> builder(caseBB);
        
        // Get function arguments for the new case
        auto args = func->arg_begin();
        auto state = args++;
        auto pc = args++;
        auto memory = args;
        
        // Get or declare the target function
        auto targetFunc = M.getOrInsertFunction(mapping.second, funcTy);
        
        // Call the target function with the same arguments
        auto call = builder.CreateCall(targetFunc, {state, pc, memory});
        builder.CreateRet(call);
        
        // Add this case to the switch
        existingSwitch->addCase(llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), mapping.first), caseBB);
        VLOG(1) << "Added new case for address 0x" << std::hex << mapping.first;
    }
    
    // Verify the function
    std::string err;
    llvm::raw_string_ostream errStream(err);
    if (llvm::verifyFunction(*func, &errStream)) {
        LOG(ERROR) << "Failed to verify __remill_missing_block: " << err;
        return;
    }
    
    VLOG(1) << "Successfully updated missing block handler with " << addr_to_func.size() << " new mappings";
}

llvm::Function* CreateEntryWithState(llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName) {
    auto& context = M.getContext();

    // check if we need to merge modules (if not merged yet)
    if (M.getFunction("entry") != nullptr) {
        LOG(INFO) << "Modules already merged";
        return nullptr;
    }

    // Read and merge the prebuilt bitcode modules
    llvm::SMDiagnostic Err;
    std::string utils_path = std::string(CMAKE_BINARY_DIR) + "/Utils.ll";
    LOG(INFO) << "Attempting to load Utils.ll from: " << utils_path;

    auto utils_module = llvm::parseIRFile(
        utils_path.c_str(),
        Err,
        M.getContext());
    if (!utils_module)
    {
        LOG(ERROR) << "Failed to load Utils.ll module: " << Err.getMessage().str();
        return nullptr;
    }
    MiscUtils::MergeModules(M, *utils_module);

    // Create function type for entry: void entry()
    auto voidTy = llvm::Type::getVoidTy(context);
    auto funcTy = llvm::FunctionType::get(voidTy, false);
    
    // Create the entry function
    auto func = llvm::Function::Create(
        funcTy, 
        llvm::Function::ExternalLinkage,
        "entry", 
        &M);

    // Create entry basic block
    auto entryBB = llvm::BasicBlock::Create(context, "entry", func);
    llvm::IRBuilder<> builder(entryBB);

    // Verify the utility module before merging
    std::string verify_err;
    llvm::raw_string_ostream errStream(verify_err);
    if (llvm::verifyModule(*utils_module, &errStream)) {
        LOG(ERROR) << "Utils module is not valid: " << verify_err;
        return nullptr;
    }

    // Get the utility functions
    auto setGSBaseFunc = M.getFunction("SetGSBase");
    auto setParamsFunc = M.getFunction("SetParameters");
    auto setStackFunc = M.getFunction("SetStack");
    auto setPCFunc = M.getFunction("SetPC");

    if (!setGSBaseFunc || !setParamsFunc || !setStackFunc || !setPCFunc) {
        LOG(ERROR) << "Failed to find utility functions:"
                  << (!setGSBaseFunc ? " SetGSBase" : "")
                  << (!setParamsFunc ? " SetParameters" : "")
                  << (!setStackFunc ? " SetStack" : "")
                  << (!setPCFunc ? " SetPC" : "");
        return nullptr;
    }

    // Call SetGSBase with GSBase parameter
    auto gsBaseVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), GSBase);
    builder.CreateCall(setGSBaseFunc, {gsBaseVal});

    // Call SetParameters
    builder.CreateCall(setParamsFunc);

    // Call SetStack
    builder.CreateCall(setStackFunc);

    // Call SetPC with PC parameter
    auto pcVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), PC);
    builder.CreateCall(setPCFunc, {pcVal});

    // Get the target function
    auto targetFunc = M.getFunction(TargetFuncName);
    if (!targetFunc) {
        LOG(ERROR) << "Failed to find target function: " << TargetFuncName;
        func->eraseFromParent();
        return nullptr;
    }

    // Get global variables
    auto stateGlobal = M.getGlobalVariable("State");
    auto memoryGlobal = M.getGlobalVariable("Memory");
    auto globalPCGlobal = M.getGlobalVariable("GlobalPC");

    if (!stateGlobal || !memoryGlobal || !globalPCGlobal) {
        LOG(ERROR) << "Failed to find required globals:"
                  << (!stateGlobal ? " State" : "")
                  << (!memoryGlobal ? " Memory" : "")
                  << (!globalPCGlobal ? " GlobalPC" : "");
        func->eraseFromParent();
        return nullptr;
    }

    // Get state pointer
    auto statePtr = builder.CreateBitCast(
        builder.CreateConstGEP1_32(stateGlobal->getValueType(), stateGlobal, 0),
        builder.getInt8PtrTy());

    // Load memory pointer
    auto memoryPtr = builder.CreateLoad(memoryGlobal->getValueType(), memoryGlobal);

    // Load PC value
    auto globalPC = builder.CreateLoad(globalPCGlobal->getValueType(), globalPCGlobal);

    // Call the target function with our constructed arguments
    builder.CreateCall(targetFunc, {statePtr, globalPC, memoryPtr});

    // Add return instruction
    builder.CreateRetVoid();

    // First verify the function
    verify_err.clear();
    if (llvm::verifyFunction(*func, &errStream)) {
        LOG(ERROR) << "Failed to verify entry function: " << verify_err;
        func->eraseFromParent();
        return nullptr;
    }

    // Then verify the entire module
    verify_err.clear();
    if (llvm::verifyModule(M, &errStream)) {
        LOG(ERROR) << "Merged module is not valid: " << verify_err;
        func->eraseFromParent();
        return nullptr;
    }

    return func;
}

} // namespace MiscUtils 