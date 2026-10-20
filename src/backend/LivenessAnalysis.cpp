#include "LivenessAnalysis.h"
#include <algorithm>
#include <iostream>

namespace kernelforge::backend {

void LivenessAnalysis::linearizeAndNumber(MachineFunction* func) {
    uint32_t currentId = 2;
    for (const auto& bb : func->blocks) {
        for (const auto& inst : bb->instructions) {
            inst->id = currentId;
            currentId += 2;
        }
    }
}

void LivenessAnalysis::compute(MachineFunction* func) {
    intervals_.clear();
    intRegToIntervalIdx_.clear();
    floatRegToIntervalIdx_.clear();
    predRegToIntervalIdx_.clear();

    linearizeAndNumber(func);

    // Map each virtual register to its definition ID and last use ID
    std::unordered_map<int, std::pair<uint32_t, uint32_t>> intRanges;
    std::unordered_map<int, std::pair<uint32_t, uint32_t>> floatRanges;
    std::unordered_map<int, std::pair<uint32_t, uint32_t>> predRanges;

    auto updateRange = [](std::unordered_map<int, std::pair<uint32_t, uint32_t>>& map, int id, uint32_t instId) {
        auto it = map.find(id);
        if (it == map.end()) {
            map[id] = {instId, instId};
        } else {
            it->second.first = std::min(it->second.first, instId);
            it->second.second = std::max(it->second.second, instId);
        }
    };

    for (const auto& bb : func->blocks) {
        for (const auto& inst : bb->instructions) {
            uint32_t instId = inst->id;

            if (inst->hasPredicate && !inst->predReg.isPhysical) {
                updateRange(predRanges, inst->predReg.id, instId);
            }

            for (const auto& op : inst->operands) {
                if (op.isReg() && !op.reg.isPhysical && op.reg.regClass != RegClass::Special) {
                    if (op.reg.regClass == RegClass::Int) {
                        updateRange(intRanges, op.reg.id, instId);
                    } else if (op.reg.regClass == RegClass::Float) {
                        updateRange(floatRanges, op.reg.id, instId);
                    } else if (op.reg.regClass == RegClass::Pred) {
                        updateRange(predRanges, op.reg.id, instId);
                    }
                } else if (op.isMem() && !op.reg.isPhysical && op.reg.regClass != RegClass::Special) {
                    if (op.reg.regClass == RegClass::Int) {
                        updateRange(intRanges, op.reg.id, instId);
                    }
                }
            }
        }
    }

    // Extend live intervals for loop back-edges
    for (const auto& bb : func->blocks) {
        if (bb->instructions.empty()) continue;
        uint32_t bbEnd = bb->instructions.back()->id;
        for (const auto& succ : bb->successors) {
            if (!succ || succ->instructions.empty()) continue;
            uint32_t succStart = succ->instructions.front()->id;
            if (succStart <= bbEnd) {
                // Back-edge from bb to succ
                for (auto& [id, range] : intRanges) {
                    if (range.first <= bbEnd && range.second >= succStart) {
                        range.second = std::max(range.second, bbEnd);
                    }
                }
                for (auto& [id, range] : floatRanges) {
                    if (range.first <= bbEnd && range.second >= succStart) {
                        range.second = std::max(range.second, bbEnd);
                    }
                }
                for (auto& [id, range] : predRanges) {
                    if (range.first <= bbEnd && range.second >= succStart) {
                        range.second = std::max(range.second, bbEnd);
                    }
                }
            }
        }
    }

    // Build intervals
    for (const auto& [id, range] : intRanges) {
        LiveInterval li;
        li.reg.regClass = RegClass::Int;
        li.reg.id = id;
        li.reg.isPhysical = false;
        li.start = range.first;
        li.end = range.second;
        intRegToIntervalIdx_[id] = intervals_.size();
        intervals_.push_back(li);
    }

    for (const auto& [id, range] : floatRanges) {
        LiveInterval li;
        li.reg.regClass = RegClass::Float;
        li.reg.id = id;
        li.reg.isPhysical = false;
        li.start = range.first;
        li.end = range.second;
        floatRegToIntervalIdx_[id] = intervals_.size();
        intervals_.push_back(li);
    }

    for (const auto& [id, range] : predRanges) {
        LiveInterval li;
        li.reg.regClass = RegClass::Pred;
        li.reg.id = id;
        li.reg.isPhysical = false;
        li.start = range.first;
        li.end = range.second;
        predRegToIntervalIdx_[id] = intervals_.size();
        intervals_.push_back(li);
    }

    std::sort(intervals_.begin(), intervals_.end());

    // Rebuild index maps after sort
    intRegToIntervalIdx_.clear();
    floatRegToIntervalIdx_.clear();
    predRegToIntervalIdx_.clear();
    for (size_t i = 0; i < intervals_.size(); ++i) {
        if (intervals_[i].reg.regClass == RegClass::Int) {
            intRegToIntervalIdx_[intervals_[i].reg.id] = i;
        } else if (intervals_[i].reg.regClass == RegClass::Float) {
            floatRegToIntervalIdx_[intervals_[i].reg.id] = i;
        } else if (intervals_[i].reg.regClass == RegClass::Pred) {
            predRegToIntervalIdx_[intervals_[i].reg.id] = i;
        }
    }
}

const LiveInterval* LivenessAnalysis::getInterval(const Register& reg) const {
    if (reg.regClass == RegClass::Int) {
        auto it = intRegToIntervalIdx_.find(reg.id);
        if (it != intRegToIntervalIdx_.end()) return &intervals_[it->second];
    } else if (reg.regClass == RegClass::Float) {
        auto it = floatRegToIntervalIdx_.find(reg.id);
        if (it != floatRegToIntervalIdx_.end()) return &intervals_[it->second];
    } else if (reg.regClass == RegClass::Pred) {
        auto it = predRegToIntervalIdx_.find(reg.id);
        if (it != predRegToIntervalIdx_.end()) return &intervals_[it->second];
    }
    return nullptr;
}

} // namespace kernelforge::backend
