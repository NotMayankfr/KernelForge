#include "LoopInvariantCodeMotion.h"
#include "ir/Instruction.h"
#include <stack>
#include <iostream>

namespace kernelforge::opt {

std::vector<LoopInvariantCodeMotionPass::Loop>
LoopInvariantCodeMotionPass::findLoops(ir::Function* func, const ir::DominatorTree& domTree) {
    std::vector<Loop> loops;

    for (const auto& bbPtr : func->basicBlocks) {
        ir::BasicBlock* tail = bbPtr.get();
        for (ir::BasicBlock* header : tail->successors) {
            // Check if header dominates tail -> back-edge!
            if (domTree.dominates(header, tail)) {
                Loop loop;
                loop.header = header;
                loop.blocks.insert(header);
                loop.blocks.insert(tail);

                std::stack<ir::BasicBlock*> worklist;
                if (tail != header) {
                    worklist.push(tail);
                }

                while (!worklist.empty()) {
                    ir::BasicBlock* b = worklist.top();
                    worklist.pop();

                    for (ir::BasicBlock* pred : b->predecessors) {
                        if (loop.blocks.find(pred) == loop.blocks.end()) {
                            loop.blocks.insert(pred);
                            worklist.push(pred);
                        }
                    }
                }

                // Find preheader (predecessor of header outside loop)
                loop.preheader = nullptr;
                for (ir::BasicBlock* p : header->predecessors) {
                    if (loop.blocks.find(p) == loop.blocks.end()) {
                        loop.preheader = p;
                        break;
                    }
                }

                if (loop.preheader) {
                    loops.push_back(std::move(loop));
                }
            }
        }
    }

    return loops;
}

bool LoopInvariantCodeMotionPass::hoistLoopInvariants(Loop& loop) {
    bool changed = false;
    std::unordered_set<ir::Instruction*> invariantInsts;

    // Iteratively find invariant instructions in the loop
    bool foundNew = true;
    while (foundNew) {
        foundNew = false;
        for (ir::BasicBlock* bb : loop.blocks) {
            for (const auto& instPtr : bb->instructions) {
                ir::Instruction* inst = instPtr.get();

                if (inst->hasSideEffects() || dynamic_cast<ir::PhiInst*>(inst) ||
                    dynamic_cast<ir::AllocaInst*>(inst)) {
                    continue;
                }

                if (invariantInsts.find(inst) != invariantInsts.end()) {
                    continue;
                }

                // Check if all operands are loop invariant
                bool allOpsInvariant = true;
                for (size_t i = 0; i < inst->numOperands(); ++i) {
                    ir::Value* op = inst->getOperand(i);
                    if (op->isConstant() || op->isArgument()) {
                        continue;
                    }
                    if (auto* opInst = dynamic_cast<ir::Instruction*>(op)) {
                        // Defined outside loop OR already identified as invariant
                        if (loop.blocks.find(opInst->parent) == loop.blocks.end() ||
                            invariantInsts.find(opInst) != invariantInsts.end()) {
                            continue;
                        }
                    }
                    allOpsInvariant = false;
                    break;
                }

                if (allOpsInvariant) {
                    invariantInsts.insert(inst);
                    foundNew = true;
                }
            }
        }
    }

    if (invariantInsts.empty()) return false;

    // Hoist invariant instructions into preheader before its terminator
    ir::Instruction* preTerm = loop.preheader->getTerminator();
    if (!preTerm) return false;

    for (ir::BasicBlock* bb : loop.blocks) {
        std::vector<ir::Instruction*> toHoist;
        for (const auto& instPtr : bb->instructions) {
            if (invariantInsts.find(instPtr.get()) != invariantInsts.end()) {
                toHoist.push_back(instPtr.get());
            }
        }

        for (ir::Instruction* inst : toHoist) {
            auto taken = bb->removeInstruction(inst);
            if (taken) {
                loop.preheader->insertInstructionBefore(preTerm, std::move(taken));
                changed = true;
            }
        }
    }

    return changed;
}

bool LoopInvariantCodeMotionPass::runOnFunction(ir::Function* func) {
    ir::DominatorTree domTree;
    domTree.compute(func);

    auto loops = findLoops(func, domTree);
    bool changed = false;
    for (auto& loop : loops) {
        if (hoistLoopInvariants(loop)) {
            changed = true;
        }
    }
    return changed;
}

} // namespace kernelforge::opt
