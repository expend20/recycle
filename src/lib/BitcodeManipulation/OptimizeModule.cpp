#include "OptimizeModule.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/IPO/Inliner.h>

#include <glog/logging.h>

namespace BitcodeManipulation {

void OptimizeModule(llvm::Module& M, unsigned level) {
    VLOG(1) << "Starting optimization of module: " << M.getName().str() << " at level " << level;
    
    if (level == 0) {
        VLOG(1) << "Optimization level 0, skipping optimization";
        return;
    }
    
    // Convert level to LLVM's OptimizationLevel enum, defaulting to O3 for maximum inlining
    llvm::OptimizationLevel optLevel;
    switch (level) {
        case 1: optLevel = llvm::OptimizationLevel::O1; break;
        case 2: optLevel = llvm::OptimizationLevel::O2; break;
        case 3: optLevel = llvm::OptimizationLevel::O3; break;
        default: optLevel = llvm::OptimizationLevel::O3; break; // Default to O3 instead of O2
    }
    
    // Set up the pass managers for each IR unit
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;
    
    // Create a PassBuilder to set up the pipeline
    llvm::PassBuilder PB;
    
    // Register all analyses with their respective managers
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    
    // Cross-register the analysis managers
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    
    // Create the optimization pass pipeline
    llvm::ModulePassManager MPM;
    
    // First, run the AlwaysInliner pass to handle functions with the alwaysinline attribute
    MPM.addPass(llvm::AlwaysInlinerPass());
    
    // Then run the normal optimization pipeline
    llvm::ModulePassManager DefaultMPM = 
        PB.buildPerModuleDefaultPipeline(optLevel);
    
    MPM.addPass(std::move(DefaultMPM));
    
    // Additional inlining pass after standard optimizations
    // This helps catch cases where inlining becomes more profitable after other optimizations
    llvm::ModulePassManager ExtraMPM;
    ExtraMPM.addPass(llvm::AlwaysInlinerPass());
    MPM.addPass(std::move(ExtraMPM));
    
    // Run the optimization passes
    MPM.run(M, MAM);
    
    VLOG(1) << "Completed optimization of module: " << M.getName().str();
}

} // namespace BitcodeManipulation 