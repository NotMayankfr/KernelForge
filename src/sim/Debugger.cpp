#include "Debugger.h"
#include "asm/Assembler.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace kernelforge::sim {

Debugger::Debugger(Simulator& sim) : sim_(sim) {
    sim_.setDebugger(this);
}

void Debugger::addBreakpoint(uint32_t pc) {
    breakpoints_.insert(pc);
    std::cout << "Breakpoint set at PC " << pc << "\n";
}

void Debugger::removeBreakpoint(uint32_t pc) {
    breakpoints_.erase(pc);
    std::cout << "Breakpoint removed from PC " << pc << "\n";
}

bool Debugger::shouldBreak(uint32_t pc) const {
    return breakpoints_.find(pc) != breakpoints_.end();
}

void Debugger::printHelp() {
    std::cout << "KernelForge SIMT Debugger Commands:\n";
    std::cout << "  step / s            Step one instruction for current warp\n";
    std::cout << "  next / n            Step one instruction across all warps\n";
    std::cout << "  continue / c        Continue execution until breakpoint or end\n";
    std::cout << "  break / b <pc>      Set breakpoint at instruction PC\n";
    std::cout << "  delete / d <pc>     Delete breakpoint at instruction PC\n";
    std::cout << "  info break          List current breakpoints\n";
    std::cout << "  info warps          Display all warps, PCs, active masks and barrier states\n";
    std::cout << "  info reg [lane]     Display registers (%R0-%R7, %F0-%F7, %P0-%P3)\n";
    std::cout << "  mask                Display current warp's active mask & reconvergence stack\n";
    std::cout << "  mem <addr> <count>  Inspect global memory words (or x/<count>w <addr>)\n";
    std::cout << "  shared <off> <cnt>  Inspect shared memory words of current block\n";
    std::cout << "  disasm [pc]         Disassemble instructions around PC\n";
    std::cout << "  help / h            Show this help menu\n";
    std::cout << "  quit / q            Exit debugger\n";
}

void Debugger::printRegisters(const WarpState* warp, int laneIdx) {
    if (!warp) {
        std::cout << "No active warp!\n";
        return;
    }

    auto printLane = [warp](size_t l) {
        const LaneState& lane = warp->lanes[l];
        bool active = (warp->activeMask & (1ULL << l)) != 0;
        std::cout << "--- Lane " << l << " (tid: " << lane.tid_x << ") [" << (active ? "ACTIVE" : "INACTIVE") << "] ---\n";
        std::cout << "  Int:   ";
        for (int i = 0; i < 8; ++i) {
            std::cout << "%R" << i << "=" << lane.r[i] << " ";
        }
        std::cout << "\n  Float: ";
        for (int i = 0; i < 8; ++i) {
            std::cout << "%F" << i << "=" << lane.f[i] << "f ";
        }
        std::cout << "\n  Pred:  ";
        for (int i = 0; i < 4; ++i) {
            std::cout << "%P" << i << "=" << (lane.p[i] ? "1" : "0") << " ";
        }
        std::cout << "\n";
    };

    if (laneIdx >= 0 && static_cast<size_t>(laneIdx) < warp->lanes.size()) {
        printLane(laneIdx);
    } else {
        for (size_t l = 0; l < warp->lanes.size(); ++l) {
            if (warp->activeMask & (1ULL << l)) {
                printLane(l);
            }
        }
    }
}

void Debugger::printWarps() {
    auto& blocks = sim_.getBlocks();
    std::cout << "Total Blocks: " << blocks.size() << "\n";
    for (size_t b = 0; b < blocks.size(); ++b) {
        std::cout << "Block " << b << ":\n";
        for (size_t w = 0; w < blocks[b].warps.size(); ++w) {
            const WarpState& warp = blocks[b].warps[w];
            std::cout << "  Warp " << w << ": PC=" << warp.pc
                      << " Mask=0x" << std::hex << warp.activeMask << std::dec
                      << " (";
            for (int l = static_cast<int>(sim_.getConfig().warpSize) - 1; l >= 0; --l) {
                std::cout << ((warp.activeMask & (1ULL << l)) ? '1' : '0');
            }
            std::cout << ") State=" << (warp.finished ? "FINISHED" : (warp.atBarrier ? "BARRIER" : "READY"))
                      << " ReconvStackDepth=" << warp.reconvStack.size() << "\n";
        }
    }
}

void Debugger::printDisasm(uint32_t pc, uint32_t count) {
    const auto& exe = sim_.getExecutable();
    int start = std::max(0, static_cast<int>(pc) - 2);
    int end = std::min(static_cast<int>(exe.code.size()), static_cast<int>(pc + count));

    for (int i = start; i < end; ++i) {
        if (static_cast<uint32_t>(i) == pc) {
            std::cout << "=>";
        } else {
            std::cout << "  ";
        }
        std::cout << Assembler::disassembleInst(exe.code[i], static_cast<uint32_t>(i)) << "\n";
    }
}

void Debugger::handleCommand(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd.empty()) return;

    if (cmd == "step" || cmd == "s") {
        WarpState* w = sim_.getCurrentWarp();
        if (w) {
            auto& blocks = sim_.getBlocks();
            BlockState& b = blocks[w->blockId];
            sim_.stepWarp(*w, b);
            printDisasm(w->pc, 1);
        } else {
            std::cout << "Simulation finished or no ready warps.\n";
        }
    } else if (cmd == "next" || cmd == "n") {
        sim_.stepAll();
        WarpState* w = sim_.getCurrentWarp();
        if (w) {
            printDisasm(w->pc, 1);
        } else {
            std::cout << "Simulation step complete.\n";
        }
    } else if (cmd == "continue" || cmd == "c") {
        while (!sim_.isFinished()) {
            WarpState* w = sim_.getCurrentWarp();
            if (w && shouldBreak(w->pc)) {
                std::cout << "Hit breakpoint at PC " << w->pc << "\n";
                printDisasm(w->pc, 1);
                return;
            }
            sim_.stepAll();
        }
        std::cout << "Kernel execution completed.\n";
    } else if (cmd == "break" || cmd == "b") {
        uint32_t pc;
        if (iss >> pc) {
            addBreakpoint(pc);
        } else {
            std::cout << "Usage: break <pc>\n";
        }
    } else if (cmd == "delete" || cmd == "d") {
        uint32_t pc;
        if (iss >> pc) {
            removeBreakpoint(pc);
        } else {
            std::cout << "Usage: delete <pc>\n";
        }
    } else if (cmd == "info") {
        std::string sub;
        iss >> sub;
        if (sub == "break") {
            std::cout << "Breakpoints:\n";
            for (uint32_t pc : breakpoints_) {
                std::cout << "  PC " << pc << "\n";
            }
        } else if (sub == "warps") {
            printWarps();
        } else if (sub == "reg") {
            int lane = -1;
            iss >> lane;
            printRegisters(sim_.getCurrentWarp(), lane);
        } else {
            std::cout << "Unknown info command. Try 'info break', 'info warps', or 'info reg'.\n";
        }
    } else if (cmd == "mask") {
        WarpState* w = sim_.getCurrentWarp();
        if (w) {
            std::cout << "Current Warp " << w->warpId << " in Block " << w->blockId << ":\n";
            std::cout << "  Active Mask: 0x" << std::hex << w->activeMask << std::dec << " (";
            for (int l = static_cast<int>(sim_.getConfig().warpSize) - 1; l >= 0; --l) {
                std::cout << ((w->activeMask & (1ULL << l)) ? '1' : '0');
            }
            std::cout << ")\n";
            std::cout << "  Reconvergence Stack Depth: " << w->reconvStack.size() << "\n";
            for (size_t i = 0; i < w->reconvStack.size(); ++i) {
                const auto& tok = w->reconvStack[i];
                std::cout << "    [" << i << "] TargetPC=" << tok.targetPC
                          << " FallthroughPC=" << tok.fallthroughPC
                          << " ReconvPC=" << tok.reconvPC
                          << " NextMask=0x" << std::hex << tok.nextMask
                          << " RestoreMask=0x" << tok.restoreMask << std::dec
                          << " FallthroughRan=" << tok.fallthroughExecuted << "\n";
            }
        }
    } else if (cmd == "mem") {
        uint32_t addr = 0, count = 4;
        iss >> addr >> count;
        std::cout << "Global Memory [0x" << std::hex << addr << "]:\n" << std::dec;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t word = sim_.readGlobalWord(addr + i * 4);
            float f = sim_.readGlobalFloat(addr + i * 4);
            std::cout << "  +" << i * 4 << " (0x" << std::hex << (addr + i * 4) << std::dec << "): "
                      << word << " (0x" << std::hex << word << std::dec << ") / "
                      << f << "f\n";
        }
    } else if (cmd == "shared") {
        uint32_t off = 0, count = 4;
        iss >> off >> count;
        WarpState* w = sim_.getCurrentWarp();
        if (w) {
            BlockState& b = sim_.getBlocks()[w->blockId];
            std::cout << "Block " << w->blockId << " Shared Memory [0x" << std::hex << off << "]:\n" << std::dec;
            for (uint32_t i = 0; i < count; ++i) {
                if (off + (i + 1) * 4 <= b.sharedMemory.size()) {
                    uint32_t word;
                    std::memcpy(&word, &b.sharedMemory[off + i * 4], 4);
                    float f;
                    std::memcpy(&f, &b.sharedMemory[off + i * 4], 4);
                    std::cout << "  +" << i * 4 << ": " << word << " / " << f << "f\n";
                }
            }
        }
    } else if (cmd == "disasm" || cmd == "disas") {
        uint32_t pc = 0;
        WarpState* w = sim_.getCurrentWarp();
        if (iss >> pc) {
            printDisasm(pc);
        } else if (w) {
            printDisasm(w->pc);
        } else {
            printDisasm(0);
        }
    } else if (cmd == "help" || cmd == "h") {
        printHelp();
    } else if (cmd == "quit" || cmd == "q") {
        std::cout << "Exiting debugger.\n";
        exit(0);
    } else {
        std::cout << "Unknown command '" << cmd << "'. Type 'help' for available commands.\n";
    }
}

void Debugger::runInteractive() {
    std::cout << "KernelForge Virtual GPU Interactive Debugger\n";
    std::cout << "Type 'help' for commands, 'step' to step, 'continue' to run.\n\n";

    WarpState* w = sim_.getCurrentWarp();
    if (w) {
        printDisasm(w->pc, 3);
    }

    std::string line;
    while (!sim_.isFinished()) {
        std::cout << "(ksim) ";
        if (!std::getline(std::cin, line)) break;
        handleCommand(line);
    }
}

} // namespace kernelforge::sim
