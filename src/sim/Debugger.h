#pragma once

#include "Simulator.h"
#include <unordered_set>
#include <string>
#include <vector>

namespace kernelforge::sim {

class Debugger {
public:
    explicit Debugger(Simulator& sim);

    void runInteractive();
    bool shouldBreak(uint32_t pc) const;

    void addBreakpoint(uint32_t pc);
    void removeBreakpoint(uint32_t pc);
    const std::unordered_set<uint32_t>& getBreakpoints() const { return breakpoints_; }

private:
    Simulator& sim_;
    std::unordered_set<uint32_t> breakpoints_;
    bool singleStep_ = false;

    void handleCommand(const std::string& line);
    void printHelp();
    void printRegisters(const WarpState* warp, int lane = -1);
    void printWarps();
    void printDisasm(uint32_t pc, uint32_t count = 5);
};

} // namespace kernelforge::sim
