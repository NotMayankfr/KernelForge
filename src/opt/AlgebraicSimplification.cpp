#include "AlgebraicSimplification.h"
#include "ir/Instruction.h"
#include <vector>

namespace kernelforge::opt {

bool AlgebraicSimplificationPass::runOnFunction(ir::Function* func) {
    bool changed = false;
    static std::vector<std::unique_ptr<ir::Value>> constantPool;

    static ir::ConstantInt constZero(0);
    static ir::ConstantInt constOne(1);
    static ir::ConstantFloat constZeroF(0.0f);
    static ir::ConstantFloat constOneF(1.0f);
    static ir::ConstantBool constTrue(true);
    static ir::ConstantBool constFalse(false);

    for (const auto& bbPtr : func->basicBlocks) {
        ir::BasicBlock* bb = bbPtr.get();
        std::vector<ir::Instruction*> toRemove;

        for (const auto& instPtr : bb->instructions) {
            ir::Instruction* inst = instPtr.get();

            if (!inst->isBinary() && !dynamic_cast<ir::ICmpInst*>(inst) && !dynamic_cast<ir::FCmpInst*>(inst)) {
                continue;
            }

            ir::Value* lhs = inst->getOperand(0);
            ir::Value* rhs = inst->getOperand(1);
            if (!lhs || !rhs) continue;

            auto* ciL = dynamic_cast<ir::ConstantInt*>(lhs);
            auto* ciR = dynamic_cast<ir::ConstantInt*>(rhs);
            auto* cfL = dynamic_cast<ir::ConstantFloat*>(lhs);
            auto* cfR = dynamic_cast<ir::ConstantFloat*>(rhs);

            ir::Value* replacement = nullptr;

            switch (inst->opcode) {
                case ir::Opcode::Add:
                    if (ciR && ciR->value == 0) replacement = lhs;
                    else if (ciL && ciL->value == 0) replacement = rhs;
                    break;
                case ir::Opcode::FAdd:
                    if (cfR && cfR->value == 0.0f) replacement = lhs;
                    else if (cfL && cfL->value == 0.0f) replacement = rhs;
                    break;
                case ir::Opcode::Sub:
                    if (ciR && ciR->value == 0) replacement = lhs;
                    else if (lhs == rhs) replacement = &constZero;
                    break;
                case ir::Opcode::FSub:
                    if (cfR && cfR->value == 0.0f) replacement = lhs;
                    else if (lhs == rhs) replacement = &constZeroF;
                    break;
                case ir::Opcode::Mul:
                    if (ciR && ciR->value == 1) replacement = lhs;
                    else if (ciL && ciL->value == 1) replacement = rhs;
                    else if (ciR && ciR->value == 0) replacement = &constZero;
                    else if (ciL && ciL->value == 0) replacement = &constZero;
                    break;
                case ir::Opcode::FMul:
                    if (cfR && cfR->value == 1.0f) replacement = lhs;
                    else if (cfL && cfL->value == 1.0f) replacement = rhs;
                    else if (cfR && cfR->value == 0.0f) replacement = &constZeroF;
                    else if (cfL && cfL->value == 0.0f) replacement = &constZeroF;
                    break;
                case ir::Opcode::SDiv:
                    if (ciR && ciR->value == 1) replacement = lhs;
                    else if (lhs == rhs) replacement = &constOne;
                    break;
                case ir::Opcode::FDiv:
                    if (cfR && cfR->value == 1.0f) replacement = lhs;
                    else if (lhs == rhs) replacement = &constOneF;
                    break;
                case ir::Opcode::SRem:
                    if (ciR && ciR->value == 1) replacement = &constZero;
                    else if (lhs == rhs) replacement = &constZero;
                    break;
                case ir::Opcode::And:
                    if (ciR && ciR->value == 0) replacement = &constZero;
                    else if (ciL && ciL->value == 0) replacement = &constZero;
                    else if (ciR && ciR->value == -1) replacement = lhs;
                    else if (ciL && ciL->value == -1) replacement = rhs;
                    else if (lhs == rhs) replacement = lhs;
                    break;
                case ir::Opcode::Or:
                    if (ciR && ciR->value == 0) replacement = lhs;
                    else if (ciL && ciL->value == 0) replacement = rhs;
                    else if (lhs == rhs) replacement = lhs;
                    break;
                case ir::Opcode::Xor:
                    if (ciR && ciR->value == 0) replacement = lhs;
                    else if (ciL && ciL->value == 0) replacement = rhs;
                    else if (lhs == rhs) replacement = &constZero;
                    break;
                case ir::Opcode::Shl:
                case ir::Opcode::AShr:
                    if (ciR && ciR->value == 0) replacement = lhs;
                    break;
                case ir::Opcode::ICmp: {
                    auto* icmp = static_cast<ir::ICmpInst*>(inst);
                    if (lhs == rhs) {
                        if (icmp->predicate == ir::CmpPredicate::EQ ||
                            icmp->predicate == ir::CmpPredicate::SLE ||
                            icmp->predicate == ir::CmpPredicate::SGE) {
                            replacement = &constTrue;
                        } else if (icmp->predicate == ir::CmpPredicate::NE ||
                                   icmp->predicate == ir::CmpPredicate::SLT ||
                                   icmp->predicate == ir::CmpPredicate::SGT) {
                            replacement = &constFalse;
                        }
                    }
                    break;
                }
                case ir::Opcode::FCmp: {
                    auto* fcmp = static_cast<ir::FCmpInst*>(inst);
                    if (lhs == rhs) {
                        if (fcmp->predicate == ir::CmpPredicate::EQ ||
                            fcmp->predicate == ir::CmpPredicate::OLE ||
                            fcmp->predicate == ir::CmpPredicate::OGE) {
                            replacement = &constTrue;
                        } else if (fcmp->predicate == ir::CmpPredicate::NE ||
                                   fcmp->predicate == ir::CmpPredicate::OLT ||
                                   fcmp->predicate == ir::CmpPredicate::OGT) {
                            replacement = &constFalse;
                        }
                    }
                    break;
                }
                default: break;
            }

            if (replacement) {
                inst->replaceAllUsesWith(replacement);
                toRemove.push_back(inst);
                changed = true;
            }
        }

        for (ir::Instruction* inst : toRemove) {
            bb->removeInstruction(inst);
        }
    }

    return changed;
}

} // namespace kernelforge::opt
