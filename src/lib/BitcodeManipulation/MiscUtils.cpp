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

// read bitcode file
std::unique_ptr<llvm::Module> ReadBitcodeFile(const std::string& filename, llvm::LLVMContext& Context) {
    llvm::SMDiagnostic Err;
    auto Module = llvm::parseIRFile(filename, Err, Context);
    if (!Module) {
        LOG(ERROR) << "Failed to load module: " << Err.getMessage().str();
        return nullptr;
    }
    return Module;
}
} // namespace MiscUtils 