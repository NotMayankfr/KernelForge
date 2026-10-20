#include "sim/Simulator.h"
#include "sim/Debugger.h"
#include "link/Linker.h"
#include <iostream>
#include <string>
#include <vector>

using namespace kernelforge;
using namespace kernelforge::sim;

int main(int argc, char** argv) {
    std::string exeFile;
    std::string kernelName;
    SimulationConfig config;
    bool showStats = false;
    uint32_t dumpAddr = 0;
    uint32_t dumpCount = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--kernel" && i + 1 < argc) {
            kernelName = argv[++i];
        } else if (arg == "--grid" && i + 1 < argc) {
            config.gridDimX = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--block" && i + 1 < argc) {
            config.blockDimX = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--warp" && i + 1 < argc) {
            config.warpSize = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--debug") {
            config.debug = true;
        } else if (arg == "--stats") {
            showStats = true;
        } else if (arg == "--dump-mem" && i + 2 < argc) {
            dumpAddr = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 0));
            dumpCount = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 0));
        } else if (arg[0] != '-') {
            exeFile = arg;
        }
    }

    if (exeFile.empty()) {
        std::cerr << "KernelForge Virtual GPU Simulator (ksim)\n";
        std::cerr << "Usage: ksim <executable.kx> [--kernel <name>] [--grid <N>] [--block <M>] [--warp <W>] [--debug] [--stats]\n";
        return 1;
    }

    ExecutableFile exe;
    if (!Linker::readExecutable(exeFile, exe)) {
        std::cerr << "Error: failed to read executable file '" << exeFile << "'\n";
        return 1;
    }

    if (exe.kernels.empty()) {
        std::cerr << "Error: no kernels found in executable\n";
        return 1;
    }

    if (kernelName.empty()) {
        kernelName = exe.kernels[0].name;
    }

    Simulator sim(exe, config);

    // Default buffer layout for testing:
    // a: 0x1000 (1024 floats)
    // b: 0x2000 (1024 floats)
    // c: 0x3000 (1024 floats)
    // n: 256
    uint32_t addrA = 0x1000;
    uint32_t addrB = 0x2000;
    uint32_t addrC = 0x3000;
    uint32_t n = config.gridDimX * config.blockDimX;

    for (uint32_t i = 0; i < n; ++i) {
        sim.writeGlobalFloat(addrA + i * 4, static_cast<float>(i));
        sim.writeGlobalFloat(addrB + i * 4, static_cast<float>(i * 2));
        sim.writeGlobalFloat(addrC + i * 4, 0.0f);
    }

    const auto* kd = exe.getKernel(kernelName);
    uint32_t pCount = (kd && kd->paramCount > 0) ? kd->paramCount : 4;

    std::vector<uint32_t> params;
    if (pCount == 4) {
        params = {addrA, addrB, addrC, n};
    } else if (pCount == 3) {
        params = {addrA, addrB, n};
    } else if (pCount == 2) {
        params = {addrA, n};
    } else {
        params = {addrA, addrB, addrC, n};
    }
    sim.initKernel(kernelName, params);

    std::cout << "KernelForge Simulator launching '" << kernelName << "' on Grid ("
              << config.gridDimX << ", 1, 1), Block (" << config.blockDimX << ", 1, 1), WarpSize "
              << config.warpSize << " (" << (config.gridDimX * config.blockDimX) << " total threads)\n";

    if (config.debug) {
        Debugger dbg(sim);
        dbg.runInteractive();
    } else {
        sim.run();
        std::cout << "Kernel completed successfully.\n";
    }

    if (showStats || true) {
        const auto& s = sim.getStats();
        std::cout << "\n=== Simulation Statistics ===\n";
        std::cout << "Total Instructions Executed: " << s.totalInstructions << "\n";
        std::cout << "Divergent Branches:          " << s.divergentBranches << "\n";
        std::cout << "Global Memory Reads:         " << s.memoryReads << "\n";
        std::cout << "Global Memory Writes:        " << s.memoryWrites << "\n";
        std::cout << "Barrier Syncs:               " << s.barrierCount << "\n";
    }

    if (dumpCount > 0) {
        std::cout << "\n=== Dump Global Memory [0x" << std::hex << dumpAddr << " - " << dumpCount << " elements] ===\n" << std::dec;
        for (uint32_t i = 0; i < dumpCount; ++i) {
            float f = sim.readGlobalFloat(dumpAddr + i * 4);
            int32_t w = static_cast<int32_t>(sim.readGlobalWord(dumpAddr + i * 4));
            std::cout << "  [" << i << "] float=" << f << " (int=" << w << ")\n";
        }
    }

    return 0;
}
