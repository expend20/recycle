#include "MiscUtils.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Linker/Linker.h>
#include <glog/logging.h>

namespace MiscUtils {

void MergeModules(llvm::Module& M1, const llvm::Module& M2) {
    // Clone M2 to avoid modifying the original
    auto ClonedM2 = llvm::CloneModule(M2);
    
    // Link the modules
    llvm::Linker::linkModules(M1, std::move(ClonedM2));
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