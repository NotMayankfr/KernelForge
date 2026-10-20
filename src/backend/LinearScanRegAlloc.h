#pragma once

#include "VirtualISA.h"
#include "LivenessAnalysis.h"
#include <set>
#include <vector>

namespace kernelforge::backend {

class LinearScanRegAlloc {
public:
    LinearScanRegAlloc(uint32_t numIntRegs = TargetInfo::ALLOCATABLE_INT_REGS,
                       uint32_t numFloatRegs = TargetInfo::ALLOCATABLE_FLOAT_REGS,
                       uint32_t numPredRegs = TargetInfo::ALLOCATABLE_PRED_REGS);

    void allocate(MachineFunction* func);

private:
    uint32_t numIntRegs_;
    uint32_t numFloatRegs_;
    uint32_t numPredRegs_;

    void allocateClass(RegClass rClass, std::vector<LiveInterval>& intervals,
                       uint32_t numPhysRegs, uint32_t& localStackSize);
    void rewriteInstructions(MachineFunction* func, const std::vector<LiveInterval>& intervals);
};

} // namespace kernelforge::backend
