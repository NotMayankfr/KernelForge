#include "DeadCodeElimination.h"
#include "ir/Instruction.h"
#include <unordered_set>
#include <vector>
#include <queue>
#include <iostream>

namespace kernelforge::opt {

bool DeadCodeEliminationPass::runOnFunction(ir::Function* func) {
    bool changed = false;
    bool localChanged = true;
    while (localChanged) {
        localChanged = false;
        if (eliminateUnreachableBlocks(func)) {
            localChanged = true;
            changed = true;
        }
        if (eliminateDeadInstructions(func)) {
            localChanged = true;
            changed = true;
        }
    }
    return changed;
}

bool DeadCodeEliminationPass::eliminateDeadInstructions(ir::Function* func) {
    bool changed = false;
    std::vector<ir::Instruction*> worklist;

    for (const auto& bbPtr : func->basicBlocks) {
        for (const auto& instPtr : bbPtr->instructions) {
            ir::Instruction* inst = instPtr.get();
            if (!inst->hasSideEffects() && !inst->hasUses()) {
                worklist.push_back(inst);
            }
        }
    }

    while (!worklist.empty()) {
        ir::Instruction* inst = worklist.back();
        worklist.pop_back();

        if (!inst->parent || inst->hasUses() || inst->hasSideEffects()) {
            continue;
        }

        // Collect operands before deletion
        std::vector<ir::Value*> operands = inst->operands;
        ir::BasicBlock* parentBB = inst->parent;
        parentBB->removeInstruction(inst);
        changed = true;

        // Check if any operand became dead
        for (ir::Value* op : operands) {
            if (auto* opInst = dynamic_cast<ir::Instruction*>(op)) {
                if (!opInst->hasSideEffects() && !opInst->hasUses()) {
                    worklist.push_back(opInst);
                }
            }
        }
    }

    return changed;
}

bool DeadCodeEliminationPass::eliminateUnreachableBlocks(ir::Function* func) {
    ir::BasicBlock* entry = func->getEntryBlock();
    if (!entry) return false;

    std::unordered_set<ir::BasicBlock*> reachable;
    std::queue<ir::BasicBlock*> q;

    reachable.insert(entry);
    q.push(entry);

    while (!q.empty()) {
        ir::BasicBlock* bb = q.front();
        q.pop();

        for (ir::BasicBlock* succ : bb->successors) {
            if (reachable.find(succ) == reachable.end()) {
                reachable.insert(succ);
                q.push(succ);
            }
        }
    }

    std::vector<ir::BasicBlock*> unreachable;
    for (const auto& bbPtr : func->basicBlocks) {
        if (reachable.find(bbPtr.get()) == reachable.end()) {
            unreachable.push_back(bbPtr.get());
        }
    }

    if (unreachable.empty()) return false;

    for (ir::BasicBlock* deadBB : unreachable) {
        // Disconnect from successors
        for (ir::BasicBlock* succ : deadBB->successors) {
            succ->removePredecessor(deadBB);
            for (const auto& instPtr : succ->instructions) {
                if (auto* phi = dynamic_cast<ir::PhiInst*>(instPtr.get())) {
                    phi->removeIncomingBlock(deadBB);
                }
            }
        }
        func->removeBasicBlock(deadBB);
    }

    return true;
}

} // namespace kernelforge::opt
