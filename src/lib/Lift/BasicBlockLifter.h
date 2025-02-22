#pragma once

#include <remill/Arch/Arch.h>
#include "Disasm/DecodedInstruction.h"

// Class to handle lifting to LLVM IR using Remill
class BasicBlockLifter {
public:
    explicit BasicBlockLifter(llvm::LLVMContext &context);
    
    bool LiftBlock(const std::vector<DecodedInstruction>& instructions,
                   uint64_t block_addr);
    
    llvm::Module* GetModule() { return dest_module.get(); }
    std::unique_ptr<llvm::Module> TakeModule() { return std::move(dest_module); }
    void PushModule(std::unique_ptr<llvm::Module> module);

private:
    llvm::LLVMContext* context;
    std::unique_ptr<llvm::Module> dest_module;
    remill::Arch::ArchPtr arch;
    std::unique_ptr<remill::IntrinsicTable> intrinsics;
    
    bool VerifyIntrinsics();
}; 