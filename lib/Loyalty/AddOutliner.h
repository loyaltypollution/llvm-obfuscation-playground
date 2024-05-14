#include "llvm/Pass.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/PassManager.h"

namespace llvm {


struct OutlineVisitor : public InstVisitor<OutlineVisitor> {

  void visitBinaryOperator(BinaryOperator &I);
};


class OutlinerPass : public PassInfoMixin<OutlinerPass> {

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }
};

} // namespace llvm