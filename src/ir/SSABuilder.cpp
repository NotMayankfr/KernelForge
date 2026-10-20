#include "SSABuilder.h"
#include <queue>
#include <iostream>

namespace kernelforge::ir {

void SSABuilder::run(Function* func) {
    func_ = func;
    candidateAllocas_.clear();
    defBlocks_.clear();
    phiBlocks_.clear();
    blockPhiMap_.clear();
    valueStacks_.clear();
    instructionsToRemove_.clear();

    domTree_.compute(func_);

    findPromotableAllocas();
    if (candidateAllocas_.empty()) return;

    computeIDF();
    insertPhiNodes();

    BasicBlock* entry = func_->getEntryBlock();
    if (entry) {
        rename(entry);
    }

    // Remove deleted loads and stores
    for (Instruction* inst : instructionsToRemove_) {
        if (inst && inst->parent) {
            inst->parent->removeInstruction(inst);
        }
    }

    // Remove candidate allocas
    for (AllocaInst* alloca : candidateAllocas_) {
        if (alloca && alloca->parent) {
            alloca->parent->removeInstruction(alloca);
        }
    }

    // Recompute dominator tree after SSA transformation
    domTree_.compute(func_);
}

void SSABuilder::findPromotableAllocas() {
    BasicBlock* entry = func_->getEntryBlock();
    if (!entry) return;

    for (const auto& instPtr : entry->instructions) {
        if (auto* alloca = dynamic_cast<AllocaInst*>(instPtr.get())) {
            // Only promote non-array scalar variables
            if (alloca->arraySize == 0) {
                candidateAllocas_.push_back(alloca);
            }
        }
    }

    // Find all definition blocks for each alloca
    for (const auto& bbPtr : func_->basicBlocks) {
        BasicBlock* bb = bbPtr.get();
        for (const auto& instPtr : bb->instructions) {
            if (auto* store = dynamic_cast<StoreInst*>(instPtr.get())) {
                if (auto* alloca = dynamic_cast<AllocaInst*>(store->getPointer())) {
                    defBlocks_[alloca].insert(bb);
                }
            }
        }
    }
}

void SSABuilder::computeIDF() {
    for (AllocaInst* alloca : candidateAllocas_) {
        std::queue<BasicBlock*> worklist;
        std::unordered_set<BasicBlock*> inWorklist;

        for (BasicBlock* defBB : defBlocks_[alloca]) {
            worklist.push(defBB);
            inWorklist.insert(defBB);
        }

        while (!worklist.empty()) {
            BasicBlock* x = worklist.front();
            worklist.pop();
            inWorklist.erase(x);

            const auto& df = domTree_.getDominanceFrontier(x);
            for (BasicBlock* y : df) {
                if (phiBlocks_[alloca].find(y) == phiBlocks_[alloca].end()) {
                    phiBlocks_[alloca].insert(y);
                    if (defBlocks_[alloca].find(y) == defBlocks_[alloca].end() &&
                        inWorklist.find(y) == inWorklist.end()) {
                        worklist.push(y);
                        inWorklist.insert(y);
                    }
                }
            }
        }
    }
}

void SSABuilder::insertPhiNodes() {
    for (AllocaInst* alloca : candidateAllocas_) {
        for (BasicBlock* bb : phiBlocks_[alloca]) {
            std::string phiName = alloca->name + "_phi";
            auto phi = std::make_unique<PhiInst>(alloca->allocatedType, phiName, bb);
            PhiInst* phiPtr = phi.get();
            bb->insertInstructionAtBeginning(std::move(phi));
            blockPhiMap_[bb][alloca] = phiPtr;
        }
    }
}

Value* SSABuilder::getDefaultValue(AllocaInst* alloca) {
    static ConstantInt zeroI(0);
    static ConstantFloat zeroF(0.0f);
    static ConstantBool zeroB(false);

    if (alloca->allocatedType->isFloat32()) return &zeroF;
    if (alloca->allocatedType->isBool()) return &zeroB;
    return &zeroI;
}

void SSABuilder::rename(BasicBlock* bb) {
    std::vector<AllocaInst*> pushedVars;

    // 1. Process Phi nodes in this block
    if (blockPhiMap_.find(bb) != blockPhiMap_.end()) {
        for (const auto& [alloca, phi] : blockPhiMap_[bb]) {
            valueStacks_[alloca].push_back(phi);
            pushedVars.push_back(alloca);
        }
    }

    // 2. Process non-phi instructions in this block
    for (const auto& instPtr : bb->instructions) {
        Instruction* inst = instPtr.get();

        if (dynamic_cast<PhiInst*>(inst)) {
            // Already handled in blockPhiMap_
            continue;
        }

        if (auto* store = dynamic_cast<StoreInst*>(inst)) {
            if (auto* alloca = dynamic_cast<AllocaInst*>(store->getPointer())) {
                if (std::find(candidateAllocas_.begin(), candidateAllocas_.end(), alloca) != candidateAllocas_.end()) {
                    valueStacks_[alloca].push_back(store->getValue());
                    pushedVars.push_back(alloca);
                    instructionsToRemove_.insert(store);
                }
            }
        } else if (auto* load = dynamic_cast<LoadInst*>(inst)) {
            if (auto* alloca = dynamic_cast<AllocaInst*>(load->getPointer())) {
                if (std::find(candidateAllocas_.begin(), candidateAllocas_.end(), alloca) != candidateAllocas_.end()) {
                    Value* topVal = nullptr;
                    if (!valueStacks_[alloca].empty()) {
                        topVal = valueStacks_[alloca].back();
                    } else {
                        topVal = getDefaultValue(alloca);
                    }
                    load->replaceAllUsesWith(topVal);
                    instructionsToRemove_.insert(load);
                }
            }
        }
    }

    // 3. Fill incoming values for phi nodes in successor blocks
    for (BasicBlock* succ : bb->successors) {
        if (blockPhiMap_.find(succ) != blockPhiMap_.end()) {
            for (const auto& [alloca, phi] : blockPhiMap_[succ]) {
                Value* topVal = nullptr;
                if (!valueStacks_[alloca].empty()) {
                    topVal = valueStacks_[alloca].back();
                } else {
                    topVal = getDefaultValue(alloca);
                }
                phi->addIncoming(topVal, bb);
            }
        }
    }

    // 4. Recurse into dominator tree children
    for (BasicBlock* child : domTree_.getChildren(bb)) {
        rename(child);
    }

    // 5. Restore value stacks
    for (AllocaInst* alloca : pushedVars) {
        valueStacks_[alloca].pop_back();
    }
}

} // namespace kernelforge::ir
