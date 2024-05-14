#include "llvm/IR/PassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"

namespace llvm {

class StackStringPass : public PassInfoMixin<StackStringPass> {
  bool opaquepointers;
  SmallVector<GlobalVariable *, 32> genedgv;

public:
  bool handleableGV(GlobalVariable *GV);
  void processStructMembers(ConstantStruct *CS,
                            SmallVector<GlobalVariable *, 32> *unhandleablegvs,
                            SmallVector<GlobalVariable *, 32> *Globals,
                            std::set<User *> *Users, bool *breakFor);
  void processArrayMembers(ConstantArray *CA,
                           SmallVector<GlobalVariable *, 32> *unhandleablegvs,
                           SmallVector<GlobalVariable *, 32> *Globals,
                           std::set<User *> *Users, bool *breakFor);
  void HandleFunction(Function *Func);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};

} // namespace llvm