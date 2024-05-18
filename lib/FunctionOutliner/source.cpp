#include <algorithm>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

namespace llvm {
struct FunctionOutliner : public PassInfoMixin<FunctionOutliner> {
    Function *createFunc(Instruction *I, Function &parent) {
        std::vector<Type *> args;
        std::vector<Value *> needsRemap;
        Instruction *insn = I->clone();
        for (int i = 0; i < insn->getNumOperands(); i++) {
            if (auto *instArg = dyn_cast<Instruction>(insn->getOperand(i))) {
                args.push_back(insn->getOperand(i)->getType());
                needsRemap.push_back(insn->getOperand(i));
            } else if (auto funcArg = dyn_cast<Argument>(insn->getOperand(i))) {
                args.push_back(insn->getOperand(i)->getType());
                needsRemap.push_back(insn->getOperand(i));
            }
        }
        Type *returnType = nullptr;
        if (insn->getType()->isVoidTy()) {
            returnType = Type::getVoidTy(parent.getParent()->getContext());
        } else {
            returnType = insn->getType();
        }
        Function *newFunc = Function::Create(
            FunctionType::get(returnType, args, false),
            GlobalValue::LinkageTypes::InternalLinkage, "", parent.getParent());
        // remap instruction operands in function
        for (int i = 0; i < needsRemap.size(); i++) {
            insn->replaceUsesOfWith(needsRemap[i], newFunc->getArg(i));
        }
        BasicBlock *BB1 = BasicBlock::Create(parent.getParent()->getContext(),
                                             "entry", newFunc);
        IRBuilder<> builder(BB1);
        builder.Insert(insn);
        if (returnType->isVoidTy()) {
            builder.CreateRetVoid();
        } else {
            builder.CreateRet(insn);
        }
        return newFunc;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        std::vector<std::string> shouldIgnore{"alloca", "ret", "br", "switch"};
        if (F.getName() == "") {
            return PreservedAnalyses::none();
        }

        std::map<Instruction *, Function *> insnMapping;

        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
            Instruction *insn = &*I;
            Function *tempFunc = nullptr;
            if (std::find(shouldIgnore.begin(), shouldIgnore.end(),
                          insn->getOpcodeName()) == shouldIgnore.end()) {
                tempFunc = createFunc(insn, F);
            }
            insnMapping.insert({insn, tempFunc});
        }

        for (auto itr = insnMapping.begin(); itr != insnMapping.end(); ++itr) {
            Instruction *insn = itr->first;
            Function *func = itr->second;
            if (func) {
                // craft call instruction
                std::vector<Value *> args;
                for (int i = 0; i < insn->getNumOperands(); i++) {
                    // ensure we do not destory an IR by moving it into a
                    // function
                    if (auto *instArg =
                            dyn_cast<Instruction>(insn->getOperand(i))) {
                        args.push_back(insn->getOperand(i));
                    } else if (auto funcArg =
                                   dyn_cast<Argument>(insn->getOperand(i))) {
                        args.push_back(insn->getOperand(i));
                    }
                }
                Instruction *callInsn =
                    CallInst::Create(FunctionCallee(func), args);
                ReplaceInstWithInst(insn, callInsn);
            }
        }
        return PreservedAnalyses::all();
    }
    static bool isRequired() { return true; }
};
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "FunctionOutliner", "v0.1", [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "outlinefuncs") {
                            FPM.addPass(FunctionOutliner());
                            return true;
                        }
                        return false;
                    });
            }};
}
}  // namespace llvm
