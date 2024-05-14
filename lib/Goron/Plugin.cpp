#include "BogusControlFlow.h"
#include "Flattening.h"
#include "SplitBasicBlock.h"
#include "Substitution.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

llvm::PassPluginLibraryInfo getObfuscationPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "Goron", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback([](StringRef Name, FunctionPassManager &FPM,
                                              ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "obfs") {
            FPM.addPass(SplitBasicBlockPass());
            FPM.addPass(BogusControlFlowPass());
            FPM.addPass(FlatteningPass());
            return true;
          }
          return false;
        });
        PB.registerOptimizerLastEPCallback([](llvm::ModulePassManager &MPM,
                                              OptimizationLevel Level) {
            MPM.addPass(createModuleToFunctionPassAdaptor(SubstitutionPass()));
        });
      }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getObfuscationPluginInfo();
}