// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "StringEncryption.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/Support/CommandLine.h"
#include "CryptoUtils.h"
#include "Obfuscation.h"
#include "Utils.h"
#include <set>

using namespace llvm;

static cl::opt<uint32_t>
    ElementEncryptProb("strcry_prob", cl::init(100), cl::NotHidden,
                       cl::desc("Choose the probability [%] each element of "
                                "ConstantDataSequential will be "
                                "obfuscated by the -strcry pass"));
static uint32_t ElementEncryptProbTemp = 100;

namespace llvm {
struct StringEncryption : public ModulePass {
  static char ID;
  bool flag;
  bool appleptrauth;
  bool opaquepointers;
  std::map<Function * /*Function*/, GlobalVariable * /*Decryption Status*/>
      encstatus;
  std::map<GlobalVariable *, std::pair<Constant *, GlobalVariable *>> mgv2keys;
  std::map<Constant *, SmallVector<unsigned int, 16>> unencryptedindex;
  SmallVector<GlobalVariable *, 32> genedgv;
  StringEncryption() : ModulePass(ID) { this->flag = true; }

  StringEncryption(bool flag) : ModulePass(ID) { this->flag = flag; }

  StringRef getPassName() const override { return "StringEncryption"; }

  bool handleableGV(GlobalVariable *GV) {
    if (GV->hasInitializer() && !GV->getSection().startswith("llvm.") &&
        !(GV->getSection().contains("__objc") &&
          !GV->getSection().contains("array")) &&
        !GV->getName().contains("OBJC") &&
        std::find(genedgv.begin(), genedgv.end(), GV) == genedgv.end() &&
        ((GV->getLinkage() == GlobalValue::LinkageTypes::PrivateLinkage ||
          GV->getLinkage() == GlobalValue::LinkageTypes::InternalLinkage) &&
         (flag || AreUsersInOneFunction(GV))))
      return true;
    return false;
  }

  bool runOnModule(Module &M) override {
    // in runOnModule. We simple iterate function list and dispatch functions
    // to handlers
    this->appleptrauth = hasApplePtrauth(&M);
#if LLVM_VERSION_MAJOR >= 17
    this->opaquepointers = true;
#else
    this->opaquepointers = !M.getContext().supportsTypedPointers();
#endif

    for (Function &F : M)
      if (toObfuscate(flag, &F, "strenc")) {
        errs() << "Running StringEncryption On " << F.getName() << "\n";

        if (!toObfuscateUint32Option(&F, "strcry_prob",
                                     &ElementEncryptProbTemp))
          ElementEncryptProbTemp = ElementEncryptProb;

        // Check if the number of applications is correct
        if (!((ElementEncryptProbTemp > 0) &&
              (ElementEncryptProbTemp <= 100))) {
          errs() << "StringEncryption application element percentage "
                    "-strcry_prob=x must be 0 < x <= 100";
          return false;
        }
        Constant *S =
            ConstantInt::getNullValue(Type::getInt32Ty(M.getContext()));
        GlobalVariable *GV = new GlobalVariable(
            M, S->getType(), false, GlobalValue::LinkageTypes::PrivateLinkage,
            S, "StringEncryptionEncStatus");
        encstatus[&F] = GV;
        HandleFunction(&F);
      }
    return true;
  }

  void processStructMembers(ConstantStruct *CS,
                            SmallVector<GlobalVariable *, 32> *unhandleablegvs,
                            SmallVector<GlobalVariable *, 32> *Globals,
                            std::set<User *> *Users, bool *breakFor) {
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
        processStructMembers(NestedCS, unhandleablegvs, Globals, Users,
                             breakFor);
      } else if (ConstantArray *NestedCA = dyn_cast<ConstantArray>(Op)) {
        processArrayMembers(NestedCA, unhandleablegvs, Globals, Users,
                            breakFor);
      }
    }
  }

  void processArrayMembers(ConstantArray *CA,
                           SmallVector<GlobalVariable *, 32> *unhandleablegvs,
                           SmallVector<GlobalVariable *, 32> *Globals,
                           std::set<User *> *Users, bool *breakFor) {
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
        processStructMembers(NestedCS, unhandleablegvs, Globals, Users,
                             breakFor);
      } else if (ConstantArray *NestedCA = dyn_cast<ConstantArray>(Op)) {
        processArrayMembers(NestedCA, unhandleablegvs, Globals, Users,
                            breakFor);
      }
    }
  }

  void HandleFunction(Function *Func) {
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
    std::set<GlobalVariable *> objCStrings;
    std::map<GlobalVariable *, std::pair<Constant *, GlobalVariable *>> GV2Keys;
    std::map<GlobalVariable * /*old*/,
             std::pair<GlobalVariable * /*encrypted*/,
                       GlobalVariable * /*decrypt space*/>>
        old2new;

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
              objCStrings.insert(GV);
              rawStrings.insert(cast<GlobalVariable>(
                  cast<ConstantStruct>(GV->getInitializer())
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
    for (GlobalVariable *ugv : unhandleablegvs)
      if (std::find(genedgv.begin(), genedgv.end(), ugv) != genedgv.end()) {
        std::pair<Constant *, GlobalVariable *> mgv2keysval = mgv2keys[ugv];
        if (ugv->getInitializer()->getType() ==
            StructType::getTypeByName(M->getContext(),
                                      "struct.__NSConstantString_tag")) {
          GlobalVariable *rawgv =
              cast<GlobalVariable>(cast<ConstantStruct>(ugv->getInitializer())
                                       ->getOperand(2)
                                       ->stripPointerCasts());
          mgv2keysval = mgv2keys[rawgv];
          if (mgv2keysval.first && mgv2keysval.second) {
            GV2Keys[rawgv] = mgv2keysval;
          }
        } else if (mgv2keysval.first && mgv2keysval.second) {
          GV2Keys[ugv] = mgv2keysval;
        }
      }
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
      Constant *KeyConst, *EncryptedConst, *DummyConst = nullptr;
      unencryptedindex[GV] = {};
      if (intType == Type::getInt8Ty(M->getContext())) {
        std::vector<uint8_t> keys, encry, dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          if (cryptoutils->get_range(100) >= ElementEncryptProbTemp) {
            unencryptedindex[GV].emplace_back(i);
            keys.emplace_back(1);
            dummy.emplace_back(CDS->getElementAsInteger(i));
            continue;
          }
          const uint8_t K = cryptoutils->get_uint8_t();
          const uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint8_t());
        }
        KeyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint8_t>(keys));
        EncryptedConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint8_t>(encry));
        DummyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint8_t>(dummy));

      } else if (intType == Type::getInt16Ty(M->getContext())) {
        std::vector<uint16_t> keys, encry, dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          if (cryptoutils->get_range(100) >= ElementEncryptProbTemp) {
            unencryptedindex[GV].emplace_back(i);
            keys.emplace_back(1);
            dummy.emplace_back(CDS->getElementAsInteger(i));
            continue;
          }
          const uint16_t K = cryptoutils->get_uint16_t();
          const uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint16_t());
        }
        KeyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint16_t>(keys));
        EncryptedConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint16_t>(encry));
        DummyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint16_t>(dummy));
      } else if (intType == Type::getInt32Ty(M->getContext())) {
        std::vector<uint32_t> keys, encry, dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          if (cryptoutils->get_range(100) >= ElementEncryptProbTemp) {
            unencryptedindex[GV].emplace_back(i);
            keys.emplace_back(1);
            dummy.emplace_back(CDS->getElementAsInteger(i));
            continue;
          }
          const uint32_t K = cryptoutils->get_uint32_t();
          const uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint32_t());
        }
        KeyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint32_t>(keys));
        EncryptedConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint32_t>(encry));
        DummyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint32_t>(dummy));
      } else if (intType == Type::getInt64Ty(M->getContext())) {
        std::vector<uint64_t> keys, encry, dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          if (cryptoutils->get_range(100) >= ElementEncryptProbTemp) {
            unencryptedindex[GV].emplace_back(i);
            keys.emplace_back(1);
            dummy.emplace_back(CDS->getElementAsInteger(i));
            continue;
          }
          const uint64_t K = cryptoutils->get_uint64_t();
          const uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint64_t());
        }
        KeyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint64_t>(keys));
        EncryptedConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint64_t>(encry));
        DummyConst =
            ConstantDataArray::get(M->getContext(), ArrayRef<uint64_t>(dummy));
      } else {
        llvm_unreachable("Unsupported CDS Type");
      }
      // Prepare new rawGV
      GlobalVariable *EncryptedRawGV = new GlobalVariable(
          *M, EncryptedConst->getType(), false, GV->getLinkage(),
          EncryptedConst, "EncryptedString", nullptr, GV->getThreadLocalMode(),
          GV->getType()->getAddressSpace());
      genedgv.emplace_back(EncryptedRawGV);
      GlobalVariable *DecryptSpaceGV = new GlobalVariable(
          *M, DummyConst->getType(), false, GV->getLinkage(), DummyConst,
          "DecryptSpace", nullptr, GV->getThreadLocalMode(),
          GV->getType()->getAddressSpace());
      genedgv.emplace_back(DecryptSpaceGV);
      old2new[GV] = std::make_pair(EncryptedRawGV, DecryptSpaceGV);
      GV2Keys[DecryptSpaceGV] = std::make_pair(KeyConst, EncryptedRawGV);
      mgv2keys[DecryptSpaceGV] = GV2Keys[DecryptSpaceGV];
      unencryptedindex[KeyConst] = unencryptedindex[GV];
    }
    // Now prepare ObjC new GV
    for (GlobalVariable *GV : objCStrings) {
      ConstantStruct *CS = cast<ConstantStruct>(GV->getInitializer());
      GlobalVariable *oldrawString =
          cast<GlobalVariable>(CS->getOperand(2)->stripPointerCasts());
      if (old2new.find(oldrawString) ==
          old2new.end()) // Filter out zero initializers
        continue;
      GlobalVariable *EncryptedOCGV = ObjectiveCString(
          GV, "EncryptedStringObjC", old2new[oldrawString].first, CS);
      genedgv.emplace_back(EncryptedOCGV);
      GlobalVariable *DecryptSpaceOCGV = ObjectiveCString(
          GV, "DecryptSpaceObjC", old2new[oldrawString].second, CS);
      genedgv.emplace_back(DecryptSpaceOCGV);
      old2new[GV] = std::make_pair(EncryptedOCGV, DecryptSpaceOCGV);
    } // End prepare ObjC new GV
    if (GV2Keys.empty())
      return;
    // Replace Uses
    for (User *U : Users) {
      for (std::map<GlobalVariable *,
                    std::pair<GlobalVariable *, GlobalVariable *>>::iterator
               iter = old2new.begin();
           iter != old2new.end(); ++iter) {
        U->replaceUsesOfWith(iter->first, iter->second.second);
        iter->first->removeDeadConstantUsers();
      }
    } // End Replace Uses
      // CleanUp Old ObjC GVs
    for (GlobalVariable *GV : objCStrings) {
      GlobalVariable *PtrauthGV = nullptr;
      if (appleptrauth) {
        Constant *C = dyn_cast_or_null<Constant>(
            opaquepointers
                ? GV->getInitializer()
                : cast<ConstantExpr>(GV->getInitializer()->getOperand(0)));
        if (C) {
          PtrauthGV = dyn_cast<GlobalVariable>(C->getOperand(0));
          if (PtrauthGV->getSection() == "llvm.ptrauth") {
            if (ConstantExpr *CE = dyn_cast<ConstantExpr>(
                    PtrauthGV->getInitializer()->getOperand(2))) {
              if (GlobalVariable *GV2 =
                      dyn_cast<GlobalVariable>(CE->getOperand(0))) {
                if (GV->getNumUses() <= 1 &&
                    GV2->getGlobalIdentifier() == GV->getGlobalIdentifier())
                  PtrauthGV->getInitializer()->setOperand(
                      2, ConstantExpr::getPtrToInt(
                             M->getGlobalVariable(
                                 "__CFConstantStringClassReference"),
                             Type::getInt64Ty(M->getContext())));
              }
            } else if (GlobalVariable *GV2 = dyn_cast<GlobalVariable>(
                           PtrauthGV->getInitializer()->getOperand(2)))
              if (GV->getNumUses() <= 1 &&
                  GV2->getGlobalIdentifier() == GV->getGlobalIdentifier())
                PtrauthGV->getInitializer()->setOperand(
                    2, ConstantExpr::getPtrToInt(
                           M->getGlobalVariable(
                               "__CFConstantStringClassReference"),
                           Type::getInt64Ty(M->getContext())));
          }
        }
      }
      GV->removeDeadConstantUsers();
      if (GV->getNumUses() == 0) {
        GV->dropAllReferences();
        old2new.erase(GV);
        GV->eraseFromParent();
      }
      if (PtrauthGV) {
        PtrauthGV->removeDeadConstantUsers();
        if (PtrauthGV->getNumUses() == 0) {
          PtrauthGV->dropAllReferences();
          PtrauthGV->eraseFromParent();
        }
      }
    }
    // CleanUp Old Raw GVs
    for (std::map<GlobalVariable *,
                  std::pair<GlobalVariable *, GlobalVariable *>>::iterator
             iter = old2new.begin();
         iter != old2new.end(); ++iter) {
      GlobalVariable *toDelete = iter->first;
      toDelete->removeDeadConstantUsers();
      if (toDelete->getNumUses() == 0) {
        toDelete->dropAllReferences();
        toDelete->eraseFromParent();
      }
    }
    GlobalVariable *StatusGV = encstatus[Func];
    /*
       - Split Original EntryPoint BB into A and C.
       - Create new BB as Decryption BB between A and C. Adjust the terminators
         into: A (Alloca a new array containing all)
               |
               B(If not decrypted)
               |
               C
     */
    BasicBlock *A = &(Func->getEntryBlock());
    BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
    C->setName("PrecedingBlock");
    BasicBlock *B =
        BasicBlock::Create(Func->getContext(), "StringDecryptionBB", Func, C);
    // Change A's terminator to jump to B
    // We'll add new terminator to jump C later
    BranchInst *newBr = BranchInst::Create(B);
    ReplaceInstWithInst(A->getTerminator(), newBr);
    // Insert DecryptionCode
    HandleDecryptionBlock(B, C, GV2Keys);
    IRBuilder<> IRB(A->getFirstNonPHIOrDbgOrLifetime());
    // Add atomic load checking status in A
    LoadInst *LI = IRB.CreateLoad(StatusGV->getValueType(), StatusGV,
                                  "LoadEncryptionStatus");
    LI->setAtomic(
        AtomicOrdering::Acquire); // Will be released at the start of C
    LI->setAlignment(Align(4));
    Value *condition = IRB.CreateICmpEQ(
        LI, ConstantInt::get(Type::getInt32Ty(Func->getContext()), 0));
    A->getTerminator()->eraseFromParent();
    BranchInst::Create(B, C, condition, A);
    // Add StoreInst atomically in C start
    // No matter control flow is coming from A or B, the GVs must be decrypted
    StoreInst *SI =
        new StoreInst(ConstantInt::get(Type::getInt32Ty(Func->getContext()), 1),
                      StatusGV, C->getFirstNonPHIOrDbgOrLifetime());
    SI->setAlignment(Align(4));
    SI->setAtomic(AtomicOrdering::Release); // Release the lock acquired in LI
  } // End of HandleFunction

  GlobalVariable *ObjectiveCString(GlobalVariable *GV, std::string name,
                                   GlobalVariable *newString,
                                   ConstantStruct *CS) {
    Value *zero = ConstantInt::get(Type::getInt32Ty(GV->getContext()), 0);
    SmallVector<Constant *, 4> vals;
    vals.emplace_back(CS->getOperand(0));
    vals.emplace_back(CS->getOperand(1));
    Constant *GEPed = ConstantExpr::getInBoundsGetElementPtr(
        newString->getValueType(), newString, {zero, zero});
    if (GEPed->getType() == CS->getOperand(2)->getType()) {
      vals.emplace_back(GEPed);
    } else {
      Constant *BitCasted =
          ConstantExpr::getBitCast(newString, CS->getOperand(2)->getType());
      vals.emplace_back(BitCasted);
    }
    vals.emplace_back(CS->getOperand(3));
    Constant *newCS =
        ConstantStruct::get(CS->getType(), ArrayRef<Constant *>(vals));
    GlobalVariable *ObjcGV = new GlobalVariable(
        *(GV->getParent()), newCS->getType(), false, GV->getLinkage(), newCS,
        name, nullptr, GV->getThreadLocalMode(),
        GV->getType()->getAddressSpace());
    // for arm64e target on Apple LLVM
    if (appleptrauth) {
      Constant *C = dyn_cast_or_null<Constant>(
          opaquepointers ? newCS : cast<ConstantExpr>(newCS->getOperand(0)));
      GlobalVariable *PtrauthGV = dyn_cast<GlobalVariable>(C->getOperand(0));
      if (PtrauthGV && PtrauthGV->getSection() == "llvm.ptrauth") {
        GlobalVariable *NewPtrauthGV = new GlobalVariable(
            *PtrauthGV->getParent(), PtrauthGV->getValueType(), true,
            PtrauthGV->getLinkage(),
            ConstantStruct::getAnon(
                {(Constant *)PtrauthGV->getInitializer()->getOperand(0),
                 (ConstantInt *)PtrauthGV->getInitializer()->getOperand(1),
                 ConstantExpr::getPtrToInt(
                     ObjcGV, Type::getInt64Ty(ObjcGV->getContext())),
                 (ConstantInt *)PtrauthGV->getInitializer()->getOperand(3)},
                false),
            PtrauthGV->getName(), nullptr, PtrauthGV->getThreadLocalMode());
        NewPtrauthGV->setSection("llvm.ptrauth");
        NewPtrauthGV->setAlignment(Align(8));
        ObjcGV->getInitializer()->setOperand(
            0,
            ConstantExpr::getBitCast(
                NewPtrauthGV, Type::getInt32PtrTy(NewPtrauthGV->getContext())));
      }
    }
    return ObjcGV;
  }

  void HandleDecryptionBlock(
      BasicBlock *B, BasicBlock *C,
      std::map<GlobalVariable *, std::pair<Constant *, GlobalVariable *>>
          &GV2Keys) {
    IRBuilder<> IRB(B);
    Value *zero = ConstantInt::get(Type::getInt32Ty(B->getContext()), 0);
    for (std::map<GlobalVariable *,
                  std::pair<Constant *, GlobalVariable *>>::iterator iter =
             GV2Keys.begin();
         iter != GV2Keys.end(); ++iter) {
      Constant *KeyConst = iter->second.first;
      ConstantDataArray *CastedCDA = cast<ConstantDataArray>(KeyConst);
      // Prevent optimization of encrypted data
      appendToCompilerUsed(*iter->second.second->getParent(),
                           {iter->second.second});
      // Element-By-Element XOR so the fucking verifier won't complain
      // Also, this hides keys
      uint64_t realkeyoff = 0;
      for (uint64_t i = 0; i < CastedCDA->getType()->getNumElements(); i++) {
        if (unencryptedindex[KeyConst].size() &&
            std::find(unencryptedindex[KeyConst].begin(),
                      unencryptedindex[KeyConst].end(),
                      i) != unencryptedindex[KeyConst].end())
          continue;
        Value *offset =
            ConstantInt::get(Type::getInt64Ty(B->getContext()), realkeyoff);
        Value *offset2 = ConstantInt::get(Type::getInt64Ty(B->getContext()), i);
        Value *EncryptedGEP =
            IRB.CreateGEP(iter->second.second->getValueType(),
                          iter->second.second, {zero, offset});
        Value *DecryptedGEP = IRB.CreateGEP(iter->first->getValueType(),
                                            iter->first, {zero, offset2});
        LoadInst *LI = IRB.CreateLoad(CastedCDA->getElementType(), EncryptedGEP,
                                      "EncryptedChar");
        Value *XORed = IRB.CreateXor(LI, CastedCDA->getElementAsConstant(i));
        IRB.CreateStore(XORed, DecryptedGEP);
        realkeyoff++;
      }
    }
    IRB.CreateBr(C);
  } // End of HandleDecryptionBlock
};

ModulePass *createStringEncryptionPass(bool flag) {
  return new StringEncryption(flag);
}
} // namespace llvm

char StringEncryption::ID = 0;
INITIALIZE_PASS(StringEncryption, "strcry", "Enable String Encryption", false,
                false)
