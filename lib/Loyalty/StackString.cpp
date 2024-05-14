#include <set>
#include "StackString.h"
#include "CryptoUtils.h"
#include "Utils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace llvm {

bool StackStringPass::handleableGV(GlobalVariable *GV) {
  return GV->hasInitializer() && !GV->getSection().startswith("llvm.") &&
         !(GV->getSection().contains("__objc") &&
           !GV->getSection().contains("array")) &&
         !GV->getName().contains("OBJC") &&
         std::find(genedgv.begin(), genedgv.end(), GV) == genedgv.end() &&
         ((GV->getLinkage() == GlobalValue::LinkageTypes::PrivateLinkage ||
           GV->getLinkage() == GlobalValue::LinkageTypes::InternalLinkage) &&
          (AreUsersInOneFunction(GV)));
}

PreservedAnalyses StackStringPass::run(Module &M, ModuleAnalysisManager &MAM) {
#if LLVM_VERSION_MAJOR >= 17
  this->opaquepointers = true;
#else
  this->opaquepointers = !M.getContext().supportsTypedPointers();
#endif
  for (Function &F : M) {
    // errs() << "Running StringEncryption On " << F.getName() << "\n";
    Constant *S = ConstantInt::getNullValue(Type::getInt32Ty(M.getContext()));
    HandleFunction(&F);
  }
  return PreservedAnalyses::none();
}

void StackStringPass::processStructMembers(
    ConstantStruct *CS, SmallVector<GlobalVariable *, 32> *unhandleablegvs,
    SmallVector<GlobalVariable *, 32> *Globals, std::set<User *> *Users,
    bool *breakFor) {
  for (unsigned i = 0; i < CS->getNumOperands(); i++) {
    Constant *Op = CS->getOperand(i);
    if (GlobalVariable *GV =
            dyn_cast<GlobalVariable>(Op->stripPointerCasts())) {
      if (!handleableGV(GV)) {
        unhandleablegvs->emplace_back(GV);
        continue;
      }
      Users->insert(opaquepointers ? CS : Op);
      if (std::find(Globals->begin(), Globals->end(), GV) == Globals->end()) {
        Globals->emplace_back(GV);
        *breakFor = true;
      }
    } else if (ConstantStruct *NestedCS = dyn_cast<ConstantStruct>(Op)) {
      processStructMembers(NestedCS, unhandleablegvs, Globals, Users, breakFor);
    } else if (ConstantArray *NestedCA = dyn_cast<ConstantArray>(Op)) {
      processArrayMembers(NestedCA, unhandleablegvs, Globals, Users, breakFor);
    }
  }
}

void StackStringPass::processArrayMembers(
    ConstantArray *CA, SmallVector<GlobalVariable *, 32> *unhandleablegvs,
    SmallVector<GlobalVariable *, 32> *Globals, std::set<User *> *Users,
    bool *breakFor) {
  for (unsigned i = 0; i < CA->getNumOperands(); i++) {
    Constant *Op = CA->getOperand(i);
    if (GlobalVariable *GV =
            dyn_cast<GlobalVariable>(Op->stripPointerCasts())) {
      if (!handleableGV(GV)) {
        unhandleablegvs->emplace_back(GV);
        continue;
      }
      Users->insert(opaquepointers ? CA : Op);
      if (std::find(Globals->begin(), Globals->end(), GV) == Globals->end()) {
        Globals->emplace_back(GV);
        *breakFor = true;
      }
    } else if (ConstantStruct *NestedCS = dyn_cast<ConstantStruct>(Op)) {
      processStructMembers(NestedCS, unhandleablegvs, Globals, Users, breakFor);
    } else if (ConstantArray *NestedCA = dyn_cast<ConstantArray>(Op)) {
      processArrayMembers(NestedCA, unhandleablegvs, Globals, Users, breakFor);
    }
  }
}

void StackStringPass::HandleFunction(Function *Func) {
  FixFunctionConstantExpr(Func);
  SmallVector<GlobalVariable *, 32> Globals;
  std::set<User *> Users;
  for (Instruction &I : instructions(Func))
    for (Value *Op : I.operands())
      if (GlobalVariable *G =
              dyn_cast<GlobalVariable>(Op->stripPointerCasts())) {
        if (User *U = dyn_cast<User>(Op))
          Users.insert(U);
        Users.insert(&I);
        Globals.emplace_back(G);
      }
  std::set<GlobalVariable *> rawStrings;

  auto end = Globals.end();
  for (auto it = Globals.begin(); it != end; ++it) {
    end = std::remove(it + 1, end, *it);
  }
  Globals.erase(end, Globals.end());

  Module *M = Func->getParent();

  SmallVector<GlobalVariable *, 32> transedGlobals, unhandleablegvs;

  do {
    for (GlobalVariable *GV : Globals) {
      if (std::find(transedGlobals.begin(), transedGlobals.end(), GV) ==
          transedGlobals.end()) {
        bool breakThisFor = false;
        if (handleableGV(GV)) {
          if (GlobalVariable *CastedGV = dyn_cast<GlobalVariable>(
                  GV->getInitializer()->stripPointerCasts())) {
            if (std::find(Globals.begin(), Globals.end(), CastedGV) ==
                Globals.end()) {
              Globals.emplace_back(CastedGV);
              ConstantExpr *CE = dyn_cast<ConstantExpr>(GV->getInitializer());
              Users.insert(CE ? CE : GV->getInitializer());
              breakThisFor = true;
            }
          }
          if (GV->getInitializer()->getType() ==
              StructType::getTypeByName(M->getContext(),
                                        "struct.__NSConstantString_tag")) {
            rawStrings.insert(
                cast<GlobalVariable>(cast<ConstantStruct>(GV->getInitializer())
                                         ->getOperand(2)
                                         ->stripPointerCasts()));
          } else if (isa<ConstantDataSequential>(GV->getInitializer())) {
            rawStrings.insert(GV);
          } else if (ConstantStruct *CS =
                         dyn_cast<ConstantStruct>(GV->getInitializer())) {
            processStructMembers(CS, &unhandleablegvs, &Globals, &Users,
                                 &breakThisFor);
          } else if (ConstantArray *CA =
                         dyn_cast<ConstantArray>(GV->getInitializer())) {
            processArrayMembers(CA, &unhandleablegvs, &Globals, &Users,
                                &breakThisFor);
          }
        } else {
          unhandleablegvs.emplace_back(GV);
        }
        transedGlobals.emplace_back(GV);
        if (breakThisFor)
          break;
      }
    } // foreach loop
  } while (transedGlobals.size() != Globals.size());

  for (GlobalVariable *GV : rawStrings) {
    if (GV->getInitializer()->isZeroValue() ||
        GV->getInitializer()->isNullValue())
      continue;
    ConstantDataSequential *CDS =
        cast<ConstantDataSequential>(GV->getInitializer());
    Type *ElementTy = CDS->getElementType();
    if (!ElementTy->isIntegerTy()) {
      continue;
    }
    IntegerType *intType = cast<IntegerType>(ElementTy);

    BasicBlock *A = &(Func->getEntryBlock());
    BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
    C->setName("PrecedingBlock");
    BasicBlock *B =
        BasicBlock::Create(Func->getContext(), "StringDecryptionBB", Func, C);

    // Change A's terminator to jump to B
    BranchInst *newBr = BranchInst::Create(B);
    ReplaceInstWithInst(A->getTerminator(), newBr);

    // Prepare new stack string
    IRBuilder<> builder(B);
    Value *StackString = builder.CreateAlloca(CDS->getType());

    for (unsigned i = 0; i < CDS->getNumElements(); i++) {
      Value *zero = ConstantInt::get(Type::getInt64Ty(B->getContext()), 0);
      const uint64_t K = cryptoutils->get_uint64_t();
      const uint64_t V = CDS->getElementAsInteger(i);
      Value *offset = ConstantInt::get(Type::getInt64Ty(B->getContext()), i);
      Value *StackStringItr =
          builder.CreateGEP(CDS->getType(), StackString, {zero, offset});
      Value *chr = ConstantInt::get(ElementTy, K);
      builder.CreateStore(chr, StackStringItr);
      Value *key = ConstantInt::get(ElementTy, K ^ V);
      LoadInst *LI = builder.CreateLoad(CDS->getElementType(), StackStringItr);
      Value *XORed = builder.CreateXor(LI, key);
      builder.CreateStore(XORed, StackStringItr);
    }

    // Remove old rawGV references
    for (User *U : Users)
      U->replaceUsesOfWith(GV, StackString);
    GV->removeDeadConstantUsers();
    if (GV->getNumUses() == 0) {
      GV->dropAllReferences();
      GV->eraseFromParent();
    }
    builder.CreateBr(C);
  }
} // End of HandleFunction

}; // namespace llvm