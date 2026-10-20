#pragma once

#include "SIMTState.h"
#include "asm/ObjectFormat.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace kernelforge::sim {

class Debugger;

class Simulator {
public:
    Simulator(const ExecutableFile& exe, SimulationConfig config);

    void initKernel(const std::string& kernelName, const std::vector<uint32_t>& paramValues);

    // Execution control
    bool stepWarp(WarpState& warp, BlockState& block);
    bool stepAll();
    void run();

    // Memory access
    uint8_t* getGlobalMemory() { return globalMemory_.data(); }
    size_t getGlobalMemorySize() const { return globalMemory_.size(); }

    void writeGlobalWord(uint32_t addr, uint32_t val);
    uint32_t readGlobalWord(uint32_t addr) const;
    void writeGlobalFloat(uint32_t addr, float val);
    float readGlobalFloat(uint32_t addr) const;

    const SimulationStats& getStats() const { return stats_; }
    const SimulationConfig& getConfig() const { return config_; }
    const ExecutableFile& getExecutable() const { return exe_; }

    std::vector<BlockState>& getBlocks() { return blocks_; }
    WarpState* getCurrentWarp();

    void setDebugger(Debugger* dbg) { debugger_ = dbg; }
    bool isFinished() const;

private:
    ExecutableFile exe_;
    SimulationConfig config_;
    SimulationStats stats_;
    std::vector<uint8_t> globalMemory_;
    std::vector<BlockState> blocks_;
    size_t currentBlockIdx_ = 0;
    size_t currentWarpIdx_ = 0;
    Debugger* debugger_ = nullptr;
    const KernelDescriptor* currentKernel_ = nullptr;

    void handleBranch(WarpState& warp, const BytecodeInst& inst);
    void checkReconvergence(WarpState& warp);
    int32_t readOperandInt(const BytecodeInst& inst, uint8_t opClass, uint8_t opReg,
                          int32_t imm, const LaneState& lane, const BlockState& block);
    float readOperandFloat(const BytecodeInst& inst, uint8_t opClass, uint8_t opReg,
                           int32_t imm, const LaneState& lane, const BlockState& block);
};

} // namespace kernelforge::sim
