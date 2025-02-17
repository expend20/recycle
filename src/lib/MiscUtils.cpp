#include "MiscUtils.h"
#include <llvm/Support/raw_ostream.h>
#include <glog/logging.h>

namespace MiscUtils {

void MergeModules(llvm::Module& M1, const llvm::Module& M2) {
    // Merge everything from M2 into M1, if there are name conflicts, keep M1's version
    for (auto& F : M2.functions()) {
        if (M1.getFunction(F.getName())) {
            // Function already exists in M1, skip
            continue;
        }
    }
    for (auto& G : M2.globals()) {
        if (M1.getGlobalVariable(G.getName())) {
            // Global already exists in M1, skip
            continue;
        }
    }
    for (auto& T : M2.aliases()) {
        if (M1.getNamedAlias(T.getName())) {
            // Alias already exists in M1, skip
            continue;
        }
    }
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