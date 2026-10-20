#include "ConstantFolding.h"
#include "ir/Instruction.h"
#include <vector>
#include <cmath>

namespace kernelforge::opt {

bool ConstantFoldingPass::runOnFunction(ir::Function* func) {
    bool changed = false;
    static std::vector<std::unique_ptr<ir::Value>> constantPool;

    for (const auto& bbPtr : func->basicBlocks) {
        ir::BasicBlock* bb = bbPtr.get();
        std::vector<ir::Instruction*> toRemove;

        for (const auto& instPtr : bb->instructions) {
            ir::Instruction* inst = instPtr.get();

            // Fold integer binary instructions
            if (inst->isBinary() && inst->numOperands() == 2) {
                auto* ci1 = dynamic_cast<ir::ConstantInt*>(inst->getOperand(0));
                auto* ci2 = dynamic_cast<ir::ConstantInt*>(inst->getOperand(1));
                if (ci1 && ci2) {
                    int32_t v1 = ci1->value;
                    int32_t v2 = ci2->value;
                    int32_t res = 0;
                    bool fold = true;

                    switch (inst->opcode) {
                        case ir::Opcode::Add: res = v1 + v2; break;
                        case ir::Opcode::Sub: res = v1 - v2; break;
                        case ir::Opcode::Mul: res = v1 * v2; break;
                        case ir::Opcode::SDiv: res = (v2 != 0) ? (v1 / v2) : 0; break;
                        case ir::Opcode::SRem: res = (v2 != 0) ? (v1 % v2) : 0; break;
                        case ir::Opcode::Shl:  res = v1 << (v2 & 31); break;
                        case ir::Opcode::AShr: res = v1 >> (v2 & 31); break;
                        case ir::Opcode::And:  res = v1 & v2; break;
                        case ir::Opcode::Or:   res = v1 | v2; break;
                        case ir::Opcode::Xor:  res = v1 ^ v2; break;
                        default: fold = false; break;
                    }

                    if (fold) {
                        constantPool.push_back(std::make_unique<ir::ConstantInt>(res));
                        inst->replaceAllUsesWith(constantPool.back().get());
                        toRemove.push_back(inst);
                        changed = true;
                        continue;
                    }
                }

                // Fold float binary instructions
                auto* cf1 = dynamic_cast<ir::ConstantFloat*>(inst->getOperand(0));
                auto* cf2 = dynamic_cast<ir::ConstantFloat*>(inst->getOperand(1));
                if (cf1 && cf2) {
                    float v1 = cf1->value;
                    float v2 = cf2->value;
                    float res = 0.0f;
                    bool fold = true;

                    switch (inst->opcode) {
                        case ir::Opcode::FAdd: res = v1 + v2; break;
                        case ir::Opcode::FSub: res = v1 - v2; break;
                        case ir::Opcode::FMul: res = v1 * v2; break;
                        case ir::Opcode::FDiv: res = (v2 != 0.0f) ? (v1 / v2) : 0.0f; break;
                        default: fold = false; break;
                    }

                    if (fold) {
                        constantPool.push_back(std::make_unique<ir::ConstantFloat>(res));
                        inst->replaceAllUsesWith(constantPool.back().get());
                        toRemove.push_back(inst);
                        changed = true;
                        continue;
                    }
                }
            }

            // Fold comparisons
            if (auto* icmp = dynamic_cast<ir::ICmpInst*>(inst)) {
                auto* ci1 = dynamic_cast<ir::ConstantInt*>(icmp->getLHS());
                auto* ci2 = dynamic_cast<ir::ConstantInt*>(icmp->getRHS());
                if (ci1 && ci2) {
                    bool res = false;
                    switch (icmp->predicate) {
                        case ir::CmpPredicate::EQ:  res = (ci1->value == ci2->value); break;
                        case ir::CmpPredicate::NE:  res = (ci1->value != ci2->value); break;
                        case ir::CmpPredicate::SLT: res = (ci1->value < ci2->value); break;
                        case ir::CmpPredicate::SLE: res = (ci1->value <= ci2->value); break;
                        case ir::CmpPredicate::SGT: res = (ci1->value > ci2->value); break;
                        case ir::CmpPredicate::SGE: res = (ci1->value >= ci2->value); break;
                        default: break;
                    }
                    constantPool.push_back(std::make_unique<ir::ConstantBool>(res));
                    inst->replaceAllUsesWith(constantPool.back().get());
                    toRemove.push_back(inst);
                    changed = true;
                    continue;
                }
            }

            if (auto* fcmp = dynamic_cast<ir::FCmpInst*>(inst)) {
                auto* cf1 = dynamic_cast<ir::ConstantFloat*>(fcmp->getLHS());
                auto* cf2 = dynamic_cast<ir::ConstantFloat*>(fcmp->getRHS());
                if (cf1 && cf2) {
                    bool res = false;
                    switch (fcmp->predicate) {
                        case ir::CmpPredicate::EQ:  res = (cf1->value == cf2->value); break;
                        case ir::CmpPredicate::NE:  res = (cf1->value != cf2->value); break;
                        case ir::CmpPredicate::OLT: res = (cf1->value < cf2->value); break;
                        case ir::CmpPredicate::OLE: res = (cf1->value <= cf2->value); break;
                        case ir::CmpPredicate::OGT: res = (cf1->value > cf2->value); break;
                        case ir::CmpPredicate::OGE: res = (cf1->value >= cf2->value); break;
                        default: break;
                    }
                    constantPool.push_back(std::make_unique<ir::ConstantBool>(res));
                    inst->replaceAllUsesWith(constantPool.back().get());
                    toRemove.push_back(inst);
                    changed = true;
                    continue;
                }
            }

            // Fold constant conditional branches
            if (auto* br = dynamic_cast<ir::BranchInst*>(inst)) {
                bool isConstTrue = false;
                bool isConstFalse = false;
                if (auto* cb = dynamic_cast<ir::ConstantBool*>(br->getCondition())) {
                    if (cb->value) isConstTrue = true;
                    else isConstFalse = true;
                } else if (auto* ci = dynamic_cast<ir::ConstantInt*>(br->getCondition())) {
                    if (ci->value != 0) isConstTrue = true;
                    else isConstFalse = true;
                }

                if (isConstTrue || isConstFalse) {
                    ir::BasicBlock* targetBB = isConstTrue ? br->getTrueTarget() : br->getFalseTarget();
                    ir::BasicBlock* deadBB = isConstTrue ? br->getFalseTarget() : br->getTrueTarget();

                    // Update CFG
                    bb->removeSuccessor(deadBB);
                    deadBB->removePredecessor(bb);

                    // Update any phi nodes in deadBB
                    for (const auto& dInst : deadBB->instructions) {
                        if (auto* phi = dynamic_cast<ir::PhiInst*>(dInst.get())) {
                            phi->removeIncomingBlock(bb);
                        }
                    }

                    // Replace branch with jump
                    auto jmp = std::make_unique<ir::JumpInst>(targetBB);
                    bb->insertInstructionBefore(br, std::move(jmp));
                    toRemove.push_back(br);
                    changed = true;
                }
            }
        }

        for (ir::Instruction* inst : toRemove) {
            bb->removeInstruction(inst);
        }
    }

    return changed;
}

} // namespace kernelforge::opt
