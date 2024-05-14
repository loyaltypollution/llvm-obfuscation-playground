#include "AddOutliner.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace llvm {

void OutlineVisitor::visitBinaryOperator(BinaryOperator &I) {
  Type *ITy = I.getType();

  if (IntegerType::classof(ITy) && !I.getMetadata("outlined")) {
    IntegerType *IntTy = static_cast<IntegerType *>(ITy);

    SmallVector<Type *, 8> types{IntTy, IntTy};
    FunctionType *outlineFt =
        FunctionType::get(IntTy, ArrayRef<Type *>(types), false);
    Function *func =
        Function::Create(outlineFt, GlobalValue::LinkageTypes::ExternalLinkage,
                         "Outliner", I.getModule());

    LLVMContext &ctx = func->getContext();
    BasicBlock *BB = BasicBlock::Create(ctx, "", func);
    IRBuilder<> builder(BB);

    Value *Arg1 = func->getArg(0);
    Value *Arg2 = func->getArg(1);
    Instruction *II = I.clone();
    II->setOperand(0, Arg1);
    II->setOperand(1, Arg2);
    Value *sum = builder.Insert(II);
    builder.CreateRet(sum);

    // attach metadata to prevent instvisitor looping forever
    LLVMContext &C = sum->getContext();
    MDNode *N = MDNode::get(C, MDString::get(C, "isoutlined"));
    cast<Instruction>(sum)->setMetadata("outlined", N);

    SmallVector<Value *, 8> params;
    params.emplace_back(I.getOperand(0));
    params.emplace_back(I.getOperand(1));
    Instruction *replace =
        CallInst::Create(outlineFt, func, ArrayRef<Value *>(params));
    ReplaceInstWithInst(cast<Instruction>(&I), replace);
  }
}

PreservedAnalyses OutlinerPass::run(Function &F, FunctionAnalysisManager &FAM) {
  OutlineVisitor *V = new OutlineVisitor();

  V->visit(F);

  return PreservedAnalyses::none();
}

}; // namespace llvm