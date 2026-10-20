#pragma once

#include "VirtualISA.h"
#include <string>

namespace kernelforge::backend {

class AsmPrinter {
public:
    static std::string print(const MachineModule* mod);
    static std::string print(const MachineFunction* func);
    static std::string print(const MachineBasicBlock* bb);
    static std::string print(const MachineInst* inst);
};

} // namespace kernelforge::backend
