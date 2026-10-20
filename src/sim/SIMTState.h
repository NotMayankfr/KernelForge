#pragma once

#include "asm/ObjectFormat.h"
#include "common/TargetInfo.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace kernelforge::sim {

struct ReconvToken {
    uint32_t targetPC = 0;
    uint32_t fallthroughPC = 0;
    uint32_t reconvPC = 0;
    uint64_t nextMask = 0;
    uint64_t restoreMask = 0;
    bool fallthroughExecuted = false;
};

struct LaneState {
    int32_t r[TargetInfo::NUM_INT_REGS] = {0};
    float f[TargetInfo::NUM_FLOAT_REGS] = {0.0f};
    bool p[TargetInfo::NUM_PRED_REGS] = {false};
    std::vector<uint8_t> localStack;

    // Special registers
    uint32_t tid_x = 0;
    uint32_t tid_y = 0;
    uint32_t tid_z = 0;
    uint32_t ctaid_x = 0;
    uint32_t ctaid_y = 0;
    uint32_t ctaid_z = 0;
    uint32_t ntid_x = 0;
    uint32_t ntid_y = 0;
    uint32_t ntid_z = 0;
    uint32_t nctaid_x = 0;
    uint32_t nctaid_y = 0;
    uint32_t nctaid_z = 0;

    LaneState() {
        localStack.resize(TargetInfo::DEFAULT_LOCAL_MEM_SIZE, 0);
    }
};

struct WarpState {
    uint32_t warpId = 0;
    uint32_t blockId = 0;
    uint32_t pc = 0;
    uint64_t activeMask = 0;
    std::vector<ReconvToken> reconvStack;
    std::vector<uint32_t> callStack;
    std::vector<LaneState> lanes;
    bool atBarrier = false;
    bool finished = false;

    bool isReady() const {
        return !finished && !atBarrier && activeMask != 0;
    }
};

struct BlockState {
    uint32_t blockIdx_x = 0;
    uint32_t blockIdx_y = 0;
    uint32_t blockIdx_z = 0;
    std::vector<uint8_t> sharedMemory;
    std::vector<WarpState> warps;

    BlockState() {
        sharedMemory.resize(TargetInfo::DEFAULT_SHARED_MEM_SIZE, 0);
    }

    bool isFinished() const {
        for (const auto& w : warps) {
            if (!w.finished) return false;
        }
        return true;
    }

    void checkBarrier() {
        // If all unfinished warps are at barrier, release them
        bool allAtBarrier = true;
        for (const auto& w : warps) {
            if (!w.finished && !w.atBarrier) {
                allAtBarrier = false;
                break;
            }
        }
        if (allAtBarrier) {
            for (auto& w : warps) {
                w.atBarrier = false;
            }
        }
    }
};

struct SimulationConfig {
    uint32_t gridDimX = 1;
    uint32_t gridDimY = 1;
    uint32_t gridDimZ = 1;
    uint32_t blockDimX = 64;
    uint32_t blockDimY = 1;
    uint32_t blockDimZ = 1;
    uint32_t warpSize = 8;
    bool debug = false;
};

struct SimulationStats {
    uint64_t totalInstructions = 0;
    uint64_t divergentBranches = 0;
    uint64_t memoryReads = 0;
    uint64_t memoryWrites = 0;
    uint64_t barrierCount = 0;
};

} // namespace kernelforge::sim
