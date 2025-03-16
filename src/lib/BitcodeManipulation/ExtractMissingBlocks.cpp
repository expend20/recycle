#include "ExtractMissingBlocks.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>

#include <glog/logging.h>
#include <set>
#include <iomanip>

namespace BitcodeManipulation {

void PrintMissingBlocks(const std::vector<uint64_t>& blocks) {
    LOG(INFO) << "Found " << blocks.size() << " missing blocks:";
    for (const auto& addr : blocks) {
        LOG(INFO) << "  0x" << std::hex << std::setfill('0') << std::setw(16) << addr;
    }
}

std::vector<uint64_t> ExtractMissingBlocks(llvm::Module& M, const std::string& function_name) {
    VLOG(1) << "Extracting missing blocks from module: " << M.getName().str();
    
    // Use a set to store unique addresses
    std::set<uint64_t> addressSet;
    
    // Get the function declaration for __rt_missing_block
    llvm::Function* missingBlockFunc = M.getFunction(function_name);
    
    // If the function doesn't exist in the module, return empty vector
    if (!missingBlockFunc) {
        VLOG(1) << "No __rt_missing_block function found in module";
        return {};
    }
    
    // Iterate through all functions in the module
    for (auto& F : M) {
        // Skip the declaration of __rt_missing_block itself
        if (&F == missingBlockFunc) {
            continue;
        }
        
        // Iterate through all basic blocks in the function
        for (auto& BB : F) {
            // Iterate through all instructions in the basic block
            for (auto& I : BB) {
                // Check if the instruction is a call to __rt_missing_block
                if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    if (callInst->getCalledFunction() == missingBlockFunc) {
                        // The second argument (index 1) is the destination address
                        if (callInst->arg_size() > 1) {
                            // Try to extract the constant integer value
                            if (auto* constArg = llvm::dyn_cast<llvm::ConstantInt>(callInst->getArgOperand(1))) {
                                uint64_t destAddr = constArg->getZExtValue();
                                addressSet.insert(destAddr);
                                VLOG(2) << "Found missing block address: 0x" << std::hex << destAddr;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Convert set to vector for return
    std::vector<uint64_t> result(addressSet.begin(), addressSet.end());
    
    VLOG(1) << "Extracted " << result.size() << " unique missing block addresses";
    
    return result;
}

} // namespace BitcodeManipulation 