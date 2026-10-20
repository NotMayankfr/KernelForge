#include "LinearScanRegAlloc.h"
#include "common/TargetInfo.h"
#include <algorithm>
#include <iostream>

namespace kernelforge::backend {

static constexpr uint32_t SCRATCH_REG1 = TargetInfo::NUM_INT_REGS - 4; // R12 / F12
static constexpr uint32_t SCRATCH_REG2 = TargetInfo::NUM_INT_REGS - 3; // R13 / F13
static constexpr uint32_t SCRATCH_DST  = TargetInfo::NUM_INT_REGS - 2; // R14 / F14
static constexpr uint32_t BASE_REG     = TargetInfo::NUM_INT_REGS - 1; // R15 (frame/stack base)

LinearScanRegAlloc::LinearScanRegAlloc(uint32_t numIntRegs, uint32_t numFloatRegs, uint32_t numPredRegs)
    : numIntRegs_(numIntRegs), numFloatRegs_(numFloatRegs), numPredRegs_(numPredRegs) {}

void LinearScanRegAlloc::allocate(MachineFunction* func) {
    LivenessAnalysis liveness;
    liveness.compute(func);

    auto intervals = liveness.getIntervals();
    if (intervals.empty()) return;

    allocateClass(RegClass::Int, intervals, numIntRegs_, func->localStackSize);
    allocateClass(RegClass::Float, intervals, numFloatRegs_, func->localStackSize);
    allocateClass(RegClass::Pred, intervals, numPredRegs_, func->localStackSize);

    rewriteInstructions(func, intervals);
}

void LinearScanRegAlloc::allocateClass(RegClass rClass, std::vector<LiveInterval>& intervals,
                                      uint32_t numPhysRegs, uint32_t& localStackSize) {
    // Collect intervals for this class
    std::vector<size_t> classIntervalIndices;
    for (size_t i = 0; i < intervals.size(); ++i) {
        if (intervals[i].reg.regClass == rClass) {
            classIntervalIndices.push_back(i);
        }
    }

    if (classIntervalIndices.empty()) return;

    std::set<int> freeRegs;
    for (uint32_t r = 0; r < numPhysRegs; ++r) {
        freeRegs.insert(r);
    }

    // Active intervals sorted by end point
    std::vector<size_t> active;

    for (size_t idx : classIntervalIndices) {
        LiveInterval& current = intervals[idx];

        // Expire old intervals
        auto it = active.begin();
        while (it != active.end()) {
            LiveInterval& act = intervals[*it];
            if (act.end < current.start) {
                freeRegs.insert(act.assignedPhysReg);
                it = active.erase(it);
            } else {
                ++it;
            }
        }

        if (!freeRegs.empty()) {
            int allocated = *freeRegs.begin();
            freeRegs.erase(freeRegs.begin());
            current.assignedPhysReg = allocated;
            current.isSpilled = false;

            // Insert into active sorted by end point
            auto insertPos = std::upper_bound(active.begin(), active.end(), idx,
                [&intervals](size_t a, size_t b) {
                    return intervals[a].end < intervals[b].end;
                });
            active.insert(insertPos, idx);
        } else {
            // Spill: compare current with active ending furthest
            size_t spillIdx = active.back();
            if (intervals[spillIdx].end > current.end) {
                // Spill the active interval
                intervals[spillIdx].isSpilled = true;
                intervals[spillIdx].spillOffset = localStackSize;
                localStackSize += 4;

                current.assignedPhysReg = intervals[spillIdx].assignedPhysReg;
                current.isSpilled = false;

                active.pop_back();
                auto insertPos = std::upper_bound(active.begin(), active.end(), idx,
                    [&intervals](size_t a, size_t b) {
                        return intervals[a].end < intervals[b].end;
                    });
                active.insert(insertPos, idx);
            } else {
                // Spill current
                current.isSpilled = true;
                current.spillOffset = localStackSize;
                localStackSize += 4;
            }
        }
    }
}

void LinearScanRegAlloc::rewriteInstructions(MachineFunction* func, const std::vector<LiveInterval>& intervals) {
    // Map (regClass, virtualId) to LiveInterval
    std::unordered_map<int, const LiveInterval*> intMap;
    std::unordered_map<int, const LiveInterval*> floatMap;
    std::unordered_map<int, const LiveInterval*> predMap;

    for (const auto& li : intervals) {
        if (li.reg.regClass == RegClass::Int) intMap[li.reg.id] = &li;
        else if (li.reg.regClass == RegClass::Float) floatMap[li.reg.id] = &li;
        else if (li.reg.regClass == RegClass::Pred) predMap[li.reg.id] = &li;
    }

    auto getLI = [&](const Register& r) -> const LiveInterval* {
        if (r.isPhysical || r.regClass == RegClass::Special) return nullptr;
        if (r.regClass == RegClass::Int) {
            auto it = intMap.find(r.id);
            return (it != intMap.end()) ? it->second : nullptr;
        } else if (r.regClass == RegClass::Float) {
            auto it = floatMap.find(r.id);
            return (it != floatMap.end()) ? it->second : nullptr;
        } else if (r.regClass == RegClass::Pred) {
            auto it = predMap.find(r.id);
            return (it != predMap.end()) ? it->second : nullptr;
        }
        return nullptr;
    };

    for (const auto& bb : func->blocks) {
        std::vector<std::unique_ptr<MachineInst>> newInsts;

        for (auto& inst : bb->instructions) {
            // Rewrite predicate register if needed
            if (inst->hasPredicate && !inst->predReg.isPhysical) {
                const LiveInterval* li = getLI(inst->predReg);
                if (li && !li->isSpilled) {
                    inst->predReg.id = li->assignedPhysReg;
                    inst->predReg.isPhysical = true;
                }
            }

            // Check if any source register needs reloading from spill slot
            for (size_t i = 1; i < inst->operands.size(); ++i) {
                if (inst->operands[i].isReg()) {
                    const LiveInterval* li = getLI(inst->operands[i].reg);
                    if (li && li->isSpilled) {
                        Register scratch;
                        scratch.regClass = li->reg.regClass;
                        scratch.id = (i == 1) ? SCRATCH_REG1 : SCRATCH_REG2;
                        scratch.isPhysical = true;

                        MachineOpcode ldOp = (li->reg.regClass == RegClass::Float) ?
                            MachineOpcode::LD_LOCAL_F32 : MachineOpcode::LD_LOCAL_S32;
                        auto reloadInst = std::make_unique<MachineInst>(ldOp);
                        reloadInst->addOperand(MachineOperand::createReg(scratch));
                        Register baseReg{RegClass::Int, BASE_REG, true, ""}; // stack base
                        reloadInst->addOperand(MachineOperand::createMem(baseReg, li->spillOffset));
                        newInsts.push_back(std::move(reloadInst));

                        inst->operands[i].reg = scratch;
                    } else if (li) {
                        inst->operands[i].reg.id = li->assignedPhysReg;
                        inst->operands[i].reg.isPhysical = true;
                    }
                } else if (inst->operands[i].isMem()) {
                    const LiveInterval* li = getLI(inst->operands[i].reg);
                    if (li && !li->isSpilled) {
                        inst->operands[i].reg.id = li->assignedPhysReg;
                        inst->operands[i].reg.isPhysical = true;
                    }
                }
            }

            // Rewrite destination operand (index 0)
            const LiveInterval* dstLI = nullptr;
            if (!inst->operands.empty()) {
                if (inst->operands[0].isReg()) {
                    dstLI = getLI(inst->operands[0].reg);
                    if (dstLI && !dstLI->isSpilled) {
                        inst->operands[0].reg.id = dstLI->assignedPhysReg;
                        inst->operands[0].reg.isPhysical = true;
                    } else if (dstLI && dstLI->isSpilled) {
                        Register scratch;
                        scratch.regClass = dstLI->reg.regClass;
                        scratch.id = SCRATCH_DST;
                        scratch.isPhysical = true;
                        inst->operands[0].reg = scratch;
                    }
                } else if (inst->operands[0].isMem()) {
                    const LiveInterval* li = getLI(inst->operands[0].reg);
                    if (li && !li->isSpilled) {
                        inst->operands[0].reg.id = li->assignedPhysReg;
                        inst->operands[0].reg.isPhysical = true;
                    }
                }
            }

            newInsts.push_back(std::move(inst));

            // If destination was spilled, insert store to spill slot
            if (dstLI && dstLI->isSpilled) {
                Register scratch;
                scratch.regClass = dstLI->reg.regClass;
                scratch.id = SCRATCH_DST;
                scratch.isPhysical = true;

                MachineOpcode stOp = (dstLI->reg.regClass == RegClass::Float) ?
                    MachineOpcode::ST_LOCAL_F32 : MachineOpcode::ST_LOCAL_S32;
                auto spillInst = std::make_unique<MachineInst>(stOp);
                Register baseReg{RegClass::Int, BASE_REG, true, ""};
                spillInst->addOperand(MachineOperand::createMem(baseReg, dstLI->spillOffset));
                spillInst->addOperand(MachineOperand::createReg(scratch));
                newInsts.push_back(std::move(spillInst));
            }
        }

        bb->instructions = std::move(newInsts);
    }
}

} // namespace kernelforge::backend
