#include "CommonSubexpressionElimination.h"
#include "ir/Instruction.h"
#include <tuple>
#include <map>
#include <vector>

namespace kernelforge::opt {

struct ExprKey {
    ir::Opcode opcode;
    ir::Value* op0;
    ir::Value* op1;
    int extra; // for cmp predicate or cast type

    bool operator<(const ExprKey& o) const {
        return std::tie(opcode, op0, op1, extra) < std::tie(o.opcode, o.op0, o.op1, o.extra);
    }
};

bool CommonSubexpressionEliminationPass::runOnFunction(ir::Function* func) {
    bool changed = false;
    ir::DominatorTree domTree;
    domTree.compute(func);

    std::map<ExprKey, std::vector<ir::Instruction*>> exprMap;

    for (const auto& bbPtr : func->basicBlocks) {
        ir::BasicBlock* bb = bbPtr.get();
        std::vector<ir::Instruction*> toRemove;

        for (const auto& instPtr : bb->instructions) {
            ir::Instruction* inst = instPtr.get();

            if (inst->hasSideEffects() || dynamic_cast<ir::PhiInst*>(inst)) {
                continue;
            }

            ExprKey key;
            key.opcode = inst->opcode;
            key.op0 = inst->numOperands() > 0 ? inst->getOperand(0) : nullptr;
            key.op1 = inst->numOperands() > 1 ? inst->getOperand(1) : nullptr;
            key.extra = 0;

            if (auto* icmp = dynamic_cast<ir::ICmpInst*>(inst)) {
                key.extra = static_cast<int>(icmp->predicate);
            } else if (auto* fcmp = dynamic_cast<ir::FCmpInst*>(inst)) {
                key.extra = static_cast<int>(fcmp->predicate);
            } else if (auto* sp = dynamic_cast<ir::GetSpecialInst*>(inst)) {
                key.extra = static_cast<int>(std::hash<std::string>{}(sp->specialReg));
            }

            // Normalize commutative operands
            if (inst->isCommutative() && key.op0 > key.op1) {
                std::swap(key.op0, key.op1);
            }

            auto it = exprMap.find(key);
            bool replaced = false;
            if (it != exprMap.end()) {
                // Find an earlier instruction that dominates this one
                for (ir::Instruction* candidate : it->second) {
                    if (domTree.dominates(candidate, inst)) {
                        inst->replaceAllUsesWith(candidate);
                        toRemove.push_back(inst);
                        changed = true;
                        replaced = true;
                        break;
                    }
                }
            }

            if (!replaced) {
                exprMap[key].push_back(inst);
            }
        }

        for (ir::Instruction* inst : toRemove) {
            bb->removeInstruction(inst);
        }
    }

    return changed;
}

} // namespace kernelforge::opt
