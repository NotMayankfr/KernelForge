#pragma once

#include "VirtualISA.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kernelforge::backend {

struct LiveInterval {
    Register reg;
    uint32_t start = 0;
    uint32_t end = 0;
    int assignedPhysReg = -1;
    bool isSpilled = false;
    int32_t spillOffset = -1;

    bool operator<(const LiveInterval& o) const {
        return start < o.start;
    }
};

class LivenessAnalysis {
public:
    void compute(MachineFunction* func);
    const std::vector<LiveInterval>& getIntervals() const { return intervals_; }
    const LiveInterval* getInterval(const Register& reg) const;

private:
    std::vector<LiveInterval> intervals_;
    std::unordered_map<int, size_t> intRegToIntervalIdx_;
    std::unordered_map<int, size_t> floatRegToIntervalIdx_;
    std::unordered_map<int, size_t> predRegToIntervalIdx_;

    void linearizeAndNumber(MachineFunction* func);
    void computeBlockLiveness(MachineFunction* func,
                              std::unordered_map<MachineBasicBlock*, std::vector<Register>>& blockDefs,
                              std::unordered_map<MachineBasicBlock*, std::vector<Register>>& blockUses);
};

} // namespace kernelforge::backend
