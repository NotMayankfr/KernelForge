#include "Simulator.h"
#include "backend/VirtualISA.h"
#include <iostream>
#include <cassert>
#include <cmath>

namespace kernelforge::sim {

Simulator::Simulator(const ExecutableFile& exe, SimulationConfig config)
    : exe_(exe), config_(config) {
    globalMemory_.resize(TargetInfo::DEFAULT_GLOBAL_MEM_SIZE, 0);
}

void Simulator::writeGlobalWord(uint32_t addr, uint32_t val) {
    if (addr + 4 <= globalMemory_.size()) {
        std::memcpy(&globalMemory_[addr], &val, 4);
    }
}

uint32_t Simulator::readGlobalWord(uint32_t addr) const {
    if (addr + 4 <= globalMemory_.size()) {
        uint32_t val;
        std::memcpy(&val, &globalMemory_[addr], 4);
        return val;
    }
    return 0;
}

void Simulator::writeGlobalFloat(uint32_t addr, float val) {
    if (addr + 4 <= globalMemory_.size()) {
        std::memcpy(&globalMemory_[addr], &val, 4);
    }
}

float Simulator::readGlobalFloat(uint32_t addr) const {
    if (addr + 4 <= globalMemory_.size()) {
        float val;
        std::memcpy(&val, &globalMemory_[addr], 4);
        return val;
    }
    return 0.0f;
}

void Simulator::initKernel(const std::string& kernelName, const std::vector<uint32_t>& paramValues) {
    currentKernel_ = exe_.getKernel(kernelName);
    if (!currentKernel_) {
        std::cerr << "Kernel '" << kernelName << "' not found in executable!\n";
        return;
    }

    blocks_.clear();
    stats_ = SimulationStats();

    uint32_t totalThreadsPerBlock = config_.blockDimX * config_.blockDimY * config_.blockDimZ;
    uint32_t warpsPerBlock = (totalThreadsPerBlock + config_.warpSize - 1) / config_.warpSize;

    // Initialize Grid Blocks
    for (uint32_t bz = 0; bz < config_.gridDimZ; ++bz) {
        for (uint32_t by = 0; by < config_.gridDimY; ++by) {
            for (uint32_t bx = 0; bx < config_.gridDimX; ++bx) {
                BlockState block;
                block.blockIdx_x = bx;
                block.blockIdx_y = by;
                block.blockIdx_z = bz;

                // Initialize Warps in this Block
                for (uint32_t w = 0; w < warpsPerBlock; ++w) {
                    WarpState warp;
                    warp.warpId = w;
                    warp.blockId = bx + by * config_.gridDimX;
                    warp.pc = currentKernel_->entryPC;
                    warp.activeMask = 0;

                    // Initialize Lanes in this Warp
                    for (uint32_t l = 0; l < config_.warpSize; ++l) {
                        uint32_t globalThreadInBlock = w * config_.warpSize + l;
                        if (globalThreadInBlock < totalThreadsPerBlock) {
                            warp.activeMask |= (1ULL << l);

                            LaneState lane;
                            lane.tid_x = globalThreadInBlock % config_.blockDimX;
                            lane.tid_y = (globalThreadInBlock / config_.blockDimX) % config_.blockDimY;
                            lane.tid_z = globalThreadInBlock / (config_.blockDimX * config_.blockDimY);

                            lane.ctaid_x = bx;
                            lane.ctaid_y = by;
                            lane.ctaid_z = bz;

                            lane.ntid_x = config_.blockDimX;
                            lane.ntid_y = config_.blockDimY;
                            lane.ntid_z = config_.blockDimZ;

                            lane.nctaid_x = config_.gridDimX;
                            lane.nctaid_y = config_.gridDimY;
                            lane.nctaid_z = config_.gridDimZ;

                            // Load kernel parameters into lane registers (%r0, %r1, ... or params)
                            for (size_t p = 0; p < paramValues.size() && p < TargetInfo::NUM_INT_REGS; ++p) {
                                lane.r[p] = static_cast<int32_t>(paramValues[p]);
                                std::memcpy(&lane.f[p], &paramValues[p], 4);
                            }

                            warp.lanes.push_back(std::move(lane));
                        }
                    }

                    block.warps.push_back(std::move(warp));
                }

                blocks_.push_back(std::move(block));
            }
        }
    }
}

bool Simulator::isFinished() const {
    for (const auto& b : blocks_) {
        if (!b.isFinished()) return false;
    }
    return true;
}

WarpState* Simulator::getCurrentWarp() {
    if (blocks_.empty()) return nullptr;
    BlockState& b = blocks_[currentBlockIdx_];
    if (b.warps.empty()) return nullptr;
    return &b.warps[currentWarpIdx_];
}

void Simulator::checkReconvergence(WarpState& warp) {
    while (!warp.reconvStack.empty()) {
        auto& top = warp.reconvStack.back();
        if (warp.pc == top.reconvPC) {
            if (!top.fallthroughExecuted) {
                // Switch to fallthrough path
                top.fallthroughExecuted = true;
                warp.pc = top.fallthroughPC;
                warp.activeMask = top.nextMask;
                return;
            } else {
                // Both branch paths completed; restore mask and pop token
                warp.activeMask = top.restoreMask;
                warp.reconvStack.pop_back();
            }
        } else {
            break;
        }
    }
}

int32_t Simulator::readOperandInt(const BytecodeInst& inst, uint8_t opClass, uint8_t opReg,
                                  int32_t imm, const LaneState& lane, const BlockState& block) {
    switch (opClass) {
        case 1: // Int register
            return (opReg < TargetInfo::NUM_INT_REGS) ? lane.r[opReg] : 0;
        case 2: // Float register as int bits
            if (opReg < TargetInfo::NUM_FLOAT_REGS) {
                int32_t val;
                std::memcpy(&val, &lane.f[opReg], 4);
                return val;
            }
            return 0;
        case 3: // Predicate register
            return (opReg < TargetInfo::NUM_PRED_REGS) ? (lane.p[opReg] ? 1 : 0) : 0;
        case 4: // Special register
            switch (opReg) {
                case 0: return lane.tid_x;
                case 1: return lane.tid_y;
                case 2: return lane.tid_z;
                case 3: return lane.ctaid_x;
                case 4: return lane.ctaid_y;
                case 5: return lane.ctaid_z;
                case 6: return lane.ntid_x;
                case 7: return lane.ntid_y;
                case 8: return lane.ntid_z;
                case 9: return lane.nctaid_x;
                case 10: return lane.nctaid_y;
                case 11: return lane.nctaid_z;
            }
            return 0;
        case 5: // Memory address: baseReg + offset
            return ((opReg < TargetInfo::NUM_INT_REGS) ? lane.r[opReg] : 0) + imm;
        case 6: // Immediate Int
        case 7: // Immediate Float bits
            return imm;
    }
    return 0;
}

float Simulator::readOperandFloat(const BytecodeInst& inst, uint8_t opClass, uint8_t opReg,
                                  int32_t imm, const LaneState& lane, const BlockState& block) {
    if (opClass == 2) {
        return (opReg < TargetInfo::NUM_FLOAT_REGS) ? lane.f[opReg] : 0.0f;
    } else if (opClass == 7) {
        float f;
        std::memcpy(&f, &imm, 4);
        return f;
    }
    int32_t iVal = readOperandInt(inst, opClass, opReg, imm, lane, block);
    float fVal;
    std::memcpy(&fVal, &iVal, 4);
    return fVal;
}

void Simulator::handleBranch(WarpState& warp, const BytecodeInst& inst) {
    uint32_t targetPC = static_cast<uint32_t>(inst.immOrOffset);
    uint32_t fallthroughPC = warp.pc + 1;

    // Check if predicated
    if (inst.flags & 1) {
        bool invert = (inst.flags & 2) != 0;
        uint8_t predId = (inst.flags >> 2) & 0x3F;

        uint64_t takenMask = 0;
        for (size_t l = 0; l < warp.lanes.size(); ++l) {
            if (warp.activeMask & (1ULL << l)) {
                bool pVal = warp.lanes[l].p[predId];
                if (invert) pVal = !pVal;
                if (pVal) takenMask |= (1ULL << l);
            }
        }

        uint64_t notTakenMask = warp.activeMask & ~takenMask;

        if (takenMask == warp.activeMask) {
            // All active lanes take branch
            warp.pc = targetPC;
        } else if (notTakenMask == warp.activeMask) {
            // No lanes take branch
            warp.pc = fallthroughPC;
        } else {
            // DIVERGENCE!
            stats_.divergentBranches++;
            ReconvToken token;
            token.targetPC = targetPC;
            token.fallthroughPC = fallthroughPC;

            // Calculate join point (immediate post-dominator / reconvergence PC)
            uint32_t joinCandidate = std::max(targetPC, fallthroughPC);
            if (fallthroughPC < exe_.code.size()) {
                const auto& nextInst = exe_.code[fallthroughPC];
                if (static_cast<backend::MachineOpcode>(nextInst.opcode) == backend::MachineOpcode::BRA &&
                    !(nextInst.flags & 1)) {
                    uint32_t falseTarget = static_cast<uint32_t>(nextInst.immOrOffset);
                    joinCandidate = std::max(joinCandidate, falseTarget);
                }
            }
            for (uint32_t p = targetPC; p < exe_.code.size() && p < targetPC + 64; ++p) {
                const auto& i = exe_.code[p];
                auto op = static_cast<backend::MachineOpcode>(i.opcode);
                if (op == backend::MachineOpcode::BRA && !(i.flags & 1)) {
                    uint32_t braTarget = static_cast<uint32_t>(i.immOrOffset);
                    if (braTarget > targetPC) {
                        joinCandidate = std::max(joinCandidate, braTarget);
                        break;
                    }
                }
            }

            token.reconvPC = joinCandidate;
            token.nextMask = notTakenMask;
            token.restoreMask = warp.activeMask;
            token.fallthroughExecuted = false;

            warp.reconvStack.push_back(token);
            warp.activeMask = takenMask;
            warp.pc = targetPC;
        }
    } else {
        // Unconditional branch
        warp.pc = targetPC;
    }
}

bool Simulator::stepWarp(WarpState& warp, BlockState& block) {
    if (!warp.isReady()) return false;
    if (warp.pc >= exe_.code.size()) {
        warp.finished = true;
        return false;
    }

    // Check if warp reached reconvergence point
    checkReconvergence(warp);

    const BytecodeInst& inst = exe_.code[warp.pc];
    auto op = static_cast<backend::MachineOpcode>(inst.opcode);
    stats_.totalInstructions++;


    // Handle Control Flow
    if (op == backend::MachineOpcode::BRA) {
        handleBranch(warp, inst);
        return true;
    }

    if (op == backend::MachineOpcode::CALL) {
        uint32_t targetPC = static_cast<uint32_t>(inst.immOrOffset);
        warp.callStack.push_back(warp.pc + 1);
        warp.pc = targetPC;
        return true;
    }

    if (op == backend::MachineOpcode::RET || op == backend::MachineOpcode::EXIT) {
        if (!warp.callStack.empty()) {
            warp.pc = warp.callStack.back();
            warp.callStack.pop_back();
            return true;
        } else {
            warp.finished = true;
            return true;
        }
    }

    if (op == backend::MachineOpcode::BAR_SYNC) {
        warp.atBarrier = true;
        warp.pc++;
        stats_.barrierCount++;
        block.checkBarrier();
        return true;
    }

    // Execute instruction across active lanes
    for (size_t l = 0; l < warp.lanes.size(); ++l) {
        if (!(warp.activeMask & (1ULL << l))) continue;
        LaneState& lane = warp.lanes[l];

        // Predicate check
        if (inst.flags & 1) {
            bool inv = (inst.flags & 2) != 0;
            uint8_t pId = (inst.flags >> 2) & 0x3F;
            bool cond = lane.p[pId];
            if (inv) cond = !cond;
            if (!cond) continue; // Lane skipped by predicate
        }

        switch (op) {
            case backend::MachineOpcode::ADD_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a + b;
                break;
            }
            case backend::MachineOpcode::SUB_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a - b;
                break;
            }
            case backend::MachineOpcode::MUL_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a * b;
                break;
            }
            case backend::MachineOpcode::DIV_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = (b != 0) ? (a / b) : 0;
                break;
            }
            case backend::MachineOpcode::REM_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = (b != 0) ? (a % b) : 0;
                break;
            }
            case backend::MachineOpcode::SHL_B32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a << (b & 31);
                break;
            }
            case backend::MachineOpcode::SHR_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a >> (b & 31);
                break;
            }
            case backend::MachineOpcode::AND_B32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a & b;
                break;
            }
            case backend::MachineOpcode::OR_B32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a | b;
                break;
            }
            case backend::MachineOpcode::XOR_B32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a ^ b;
                break;
            }
            case backend::MachineOpcode::ADD_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                float b = readOperandFloat(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = a + b;
                break;
            }
            case backend::MachineOpcode::SUB_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                float b = readOperandFloat(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = a - b;
                break;
            }
            case backend::MachineOpcode::MUL_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                float b = readOperandFloat(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = a * b;
                break;
            }
            case backend::MachineOpcode::DIV_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                float b = readOperandFloat(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = (b != 0.0f) ? (a / b) : 0.0f;
                break;
            }
            case backend::MachineOpcode::SETP_LT_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a < b);
                break;
            }
            case backend::MachineOpcode::SETP_LE_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a <= b);
                break;
            }
            case backend::MachineOpcode::SETP_GT_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a > b);
                break;
            }
            case backend::MachineOpcode::SETP_GE_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a >= b);
                break;
            }
            case backend::MachineOpcode::SETP_EQ_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a == b);
                break;
            }
            case backend::MachineOpcode::SETP_NE_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                int32_t b = readOperandInt(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a != b);
                break;
            }
            case backend::MachineOpcode::SETP_LT_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                float b = readOperandFloat(inst, inst.src2Class, inst.src2Reg, inst.immOrOffset, lane, block);
                lane.p[inst.dstReg] = (a < b);
                break;
            }
            case backend::MachineOpcode::MOV_B32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = a;
                break;
            }
            case backend::MachineOpcode::MOV_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = a;
                break;
            }
            case backend::MachineOpcode::CVT_F32_S32: {
                int32_t a = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = static_cast<float>(a);
                break;
            }
            case backend::MachineOpcode::CVT_S32_F32: {
                float a = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = static_cast<int32_t>(a);
                break;
            }
            case backend::MachineOpcode::LD_GLOBAL_S32: {
                uint32_t addr = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                lane.r[inst.dstReg] = static_cast<int32_t>(readGlobalWord(addr));
                stats_.memoryReads++;
                break;
            }
            case backend::MachineOpcode::LD_GLOBAL_F32: {
                uint32_t addr = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                lane.f[inst.dstReg] = readGlobalFloat(addr);
                stats_.memoryReads++;
                break;
            }
            case backend::MachineOpcode::ST_GLOBAL_S32: {
                uint32_t addr = readOperandInt(inst, inst.dstClass, inst.dstReg, inst.immOrOffset, lane, block);
                int32_t val = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                writeGlobalWord(addr, static_cast<uint32_t>(val));
                stats_.memoryWrites++;
                break;
            }
            case backend::MachineOpcode::ST_GLOBAL_F32: {
                uint32_t addr = readOperandInt(inst, inst.dstClass, inst.dstReg, inst.immOrOffset, lane, block);
                float val = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                writeGlobalFloat(addr, val);
                stats_.memoryWrites++;
                break;
            }
            case backend::MachineOpcode::LD_SHARED_S32: {
                uint32_t offset = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                if (offset + 4 <= block.sharedMemory.size()) {
                    int32_t val;
                    std::memcpy(&val, &block.sharedMemory[offset], 4);
                    lane.r[inst.dstReg] = val;
                }
                stats_.memoryReads++;
                break;
            }
            case backend::MachineOpcode::LD_SHARED_F32: {
                uint32_t offset = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                if (offset + 4 <= block.sharedMemory.size()) {
                    float val;
                    std::memcpy(&val, &block.sharedMemory[offset], 4);
                    lane.f[inst.dstReg] = val;
                }
                stats_.memoryReads++;
                break;
            }
            case backend::MachineOpcode::ST_SHARED_S32: {
                uint32_t offset = readOperandInt(inst, inst.dstClass, inst.dstReg, inst.immOrOffset, lane, block);
                int32_t val = readOperandInt(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                if (offset + 4 <= block.sharedMemory.size()) {
                    std::memcpy(&block.sharedMemory[offset], &val, 4);
                }
                stats_.memoryWrites++;
                break;
            }
            case backend::MachineOpcode::ST_SHARED_F32: {
                uint32_t offset = readOperandInt(inst, inst.dstClass, inst.dstReg, inst.immOrOffset, lane, block);
                float val = readOperandFloat(inst, inst.src1Class, inst.src1Reg, inst.immOrOffset, lane, block);
                if (offset + 4 <= block.sharedMemory.size()) {
                    std::memcpy(&block.sharedMemory[offset], &val, 4);
                }
                stats_.memoryWrites++;
                break;
            }
            case backend::MachineOpcode::LD_LOCAL_S32: {
                uint32_t offset = static_cast<uint32_t>(inst.immOrOffset);
                if (offset + 4 <= lane.localStack.size()) {
                    int32_t val;
                    std::memcpy(&val, &lane.localStack[offset], 4);
                    lane.r[inst.dstReg] = val;
                }
                break;
            }
            case backend::MachineOpcode::LD_LOCAL_F32: {
                uint32_t offset = static_cast<uint32_t>(inst.immOrOffset);
                if (offset + 4 <= lane.localStack.size()) {
                    float val;
                    std::memcpy(&val, &lane.localStack[offset], 4);
                    lane.f[inst.dstReg] = val;
                }
                break;
            }
            case backend::MachineOpcode::ST_LOCAL_S32: {
                uint32_t offset = static_cast<uint32_t>(inst.immOrOffset);
                if (offset + 4 <= lane.localStack.size()) {
                    int32_t val = (inst.src1Reg < TargetInfo::NUM_INT_REGS) ? lane.r[inst.src1Reg] : 0;
                    std::memcpy(&lane.localStack[offset], &val, 4);
                }
                break;
            }
            case backend::MachineOpcode::ST_LOCAL_F32: {
                uint32_t offset = static_cast<uint32_t>(inst.immOrOffset);
                if (offset + 4 <= lane.localStack.size()) {
                    float val = (inst.src1Reg < TargetInfo::NUM_FLOAT_REGS) ? lane.f[inst.src1Reg] : 0.0f;
                    std::memcpy(&lane.localStack[offset], &val, 4);
                }
                break;
            }
            default:
                break;
        }
    }

    warp.pc++;
    return true;
}

bool Simulator::stepAll() {
    if (isFinished()) return false;

    bool steppedAny = false;
    for (size_t b = 0; b < blocks_.size(); ++b) {
        BlockState& block = blocks_[b];
        for (size_t w = 0; w < block.warps.size(); ++w) {
            WarpState& warp = block.warps[w];
            if (warp.isReady()) {
                currentBlockIdx_ = b;
                currentWarpIdx_ = w;
                stepWarp(warp, block);
                steppedAny = true;
            }
        }
    }
    return steppedAny;
}

void Simulator::run() {
    while (!isFinished()) {
        stepAll();
    }
}

} // namespace kernelforge::sim
