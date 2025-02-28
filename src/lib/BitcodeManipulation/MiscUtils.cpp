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

namespace BitcodeManipulation {

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

llvm::Function* CreateEntryWithState(
    llvm::Module& M, uint64_t PC, uint64_t GSBase, const std::string& TargetFuncName, const std::string& llFile)
{
    auto& context = M.getContext();

    // check if we need to merge modules (if not merged yet)
    if (M.getFunction("entry") != nullptr) {
        LOG(INFO) << "Modules already merged";
        return nullptr;
    }

    // Read and merge the prebuilt bitcode modules
    llvm::SMDiagnostic Err;
    std::string utils_path = std::string(CMAKE_BINARY_DIR) + "/" + llFile;
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
    BitcodeManipulation::MergeModules(M, *utils_module);

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