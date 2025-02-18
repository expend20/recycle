#include "MiscUtils.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IR/Verifier.h>
#include <glog/logging.h>

namespace MiscUtils {

// just clone the module
std::unique_ptr<llvm::Module> CloneModule(const llvm::Module& M) {
    return llvm::CloneModule(M);
}

// just as a sample
void MergeModules(llvm::Module& M1, const llvm::Module& M2) {

    LOG(INFO) << "Merging modules";

    LOG(INFO) << "Verifying M2";
    if (llvm::verifyModule(M2, &llvm::errs())) {
        LOG(ERROR) << "M2 is not valid";
        return;
    }
    LOG(INFO) << "Verifying M1";    
    if (llvm::verifyModule(M1, &llvm::errs())) {
        LOG(ERROR) << "M1 is not valid";
        return;
    }
    // Clone M2 to avoid modifying the original
    auto ClonedM2 = llvm::CloneModule(M2);
    LOG(INFO) << "Cloned M2";
    
    // Link the modules
    llvm::Linker::linkModules(M1, std::move(ClonedM2));
    LOG(INFO) << "Linked modules";
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
    llvm::outs() << "LLVM IR written to " << filename << "\n";
}

} // namespace MiscUtils 