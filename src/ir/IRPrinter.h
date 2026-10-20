#pragma once

#include "Module.h"
#include <string>
#include <iostream>

namespace kernelforge::ir {

class IRPrinter {
public:
    static std::string print(const Module* mod);
    static std::string print(const Function* func);
    static std::string print(const BasicBlock* bb);
    static std::string print(const Instruction* inst);
    static std::string print(const Value* val);
};

} // namespace kernelforge::ir
