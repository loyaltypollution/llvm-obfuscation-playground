#include "AddOutliner.h"
#include "StackString.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

static cl::opt<bool>
    EnableStackString("enable-sstring", cl::init(false), cl::NotHidden,
                      cl::desc("Enable stack string obfuscation"));

static cl::opt<bool> EnableOutliner("enable-outline", cl::init(false),
                                    cl::NotHidden,
                                    cl::desc("Enable outlining obfuscation"));

llvm::PassPluginLibraryInfo getObfuscationPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Loyalty", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                                  OptimizationLevel OL) {
              if (EnableStackString)
                MPM.addPass(StackStringPass());
              if (EnableOutliner)
                MPM.addPass(createModuleToFunctionPassAdaptor(OutlinerPass()));
              return true;
            });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getObfuscationPluginInfo();
}