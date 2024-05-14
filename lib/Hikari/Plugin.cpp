#include "Obfuscation.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

llvm::PassPluginLibraryInfo getObfuscationPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "Hikari", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback([](StringRef Name, ModulePassManager &MPM,
                                              ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "obfs") {
            MPM.addPass(ObfuscationPass());
            return true;
          }
          return false;
        });
      }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getObfuscationPluginInfo();
}