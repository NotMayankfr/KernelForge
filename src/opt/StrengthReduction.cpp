#include "StrengthReduction.h"
#include "ir/Instruction.h"
#include <vector>

namespace kernelforge::opt {

static bool isPowerOfTwo(int32_t val, int& shift) {
    if (val <= 0) return false;
    if ((val & (val - 1)) != 0) return false;
    shift = 0;
    while ((val >> shift) > 1) {
        shift++;
    }
    return true;
}

bool StrengthReductionPass::runOnFunction(ir::Function* func) {
    bool changed = false;
    static std::vector<std::unique_ptr<ir::Value>> constantPool;

    for (const auto& bbPtr : func->basicBlocks) {
        ir::BasicBlock* bb = bbPtr.get();
        std::vector<ir::Instruction*> toRemove;

        for (const auto& instPtr : bb->instructions) {
            ir::Instruction* inst = instPtr.get();

            // x + x -> x << 1
            if (inst->opcode == ir::Opcode::Add && inst->getOperand(0) == inst->getOperand(1)) {
                constantPool.push_back(std::make_unique<ir::ConstantInt>(1));
                auto shl = std::make_unique<ir::BinaryInst>(
                    ir::Opcode::Shl, inst->getOperand(0), constantPool.back().get(), inst->name + "_sr");
                inst->replaceAllUsesWith(shl.get());
                bb->insertInstructionBefore(inst, std::move(shl));
                toRemove.push_back(inst);
                changed = true;
                continue;
            }

            // x * 2^k -> x << k
            if (inst->opcode == ir::Opcode::Mul) {
                auto* ciL = dynamic_cast<ir::ConstantInt*>(inst->getOperand(0));
                auto* ciR = dynamic_cast<ir::ConstantInt*>(inst->getOperand(1));

                int shift = 0;
                ir::Value* varOp = nullptr;
                if (ciR && isPowerOfTwo(ciR->value, shift)) {
                    varOp = inst->getOperand(0);
                } else if (ciL && isPowerOfTwo(ciL->value, shift)) {
                    varOp = inst->getOperand(1);
                }

                if (varOp) {
                    constantPool.push_back(std::make_unique<ir::ConstantInt>(shift));
                    auto shl = std::make_unique<ir::BinaryInst>(
                        ir::Opcode::Shl, varOp, constantPool.back().get(), inst->name + "_sr");
                    inst->replaceAllUsesWith(shl.get());
                    bb->insertInstructionBefore(inst, std::move(shl));
                    toRemove.push_back(inst);
                    changed = true;
                    continue;
                }
            }

            // x / 2^k -> x >> k
            if (inst->opcode == ir::Opcode::SDiv) {
                auto* ciR = dynamic_cast<ir::ConstantInt*>(inst->getOperand(1));
                int shift = 0;
                if (ciR && isPowerOfTwo(ciR->value, shift)) {
                    constantPool.push_back(std::make_unique<ir::ConstantInt>(shift));
                    auto shr = std::make_unique<ir::BinaryInst>(
                        ir::Opcode::AShr, inst->getOperand(0), constantPool.back().get(), inst->name + "_sr");
                    inst->replaceAllUsesWith(shr.get());
                    bb->insertInstructionBefore(inst, std::move(shr));
                    toRemove.push_back(inst);
                    changed = true;
                    continue;
                }
            }

            // x % 2^k -> x & (2^k - 1)
            if (inst->opcode == ir::Opcode::SRem) {
                auto* ciR = dynamic_cast<ir::ConstantInt*>(inst->getOperand(1));
                int shift = 0;
                if (ciR && isPowerOfTwo(ciR->value, shift)) {
                    int32_t mask = ciR->value - 1;
                    constantPool.push_back(std::make_unique<ir::ConstantInt>(mask));
                    auto andInst = std::make_unique<ir::BinaryInst>(
                        ir::Opcode::And, inst->getOperand(0), constantPool.back().get(), inst->name + "_sr");
                    inst->replaceAllUsesWith(andInst.get());
                    bb->insertInstructionBefore(inst, std::move(andInst));
                    toRemove.push_back(inst);
                    changed = true;
                    continue;
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
