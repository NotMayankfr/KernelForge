#pragma once

#include "Function.h"
#include "Module.h"
#include "Dominators.h"
#include <string>
#include <vector>

namespace kernelforge::ir {

class IRVerifier {
public:
    static bool verify(Module* mod, std::string* errorMsg = nullptr);
    static bool verify(Function* func, std::string* errorMsg = nullptr);

private:
    Function* func_ = nullptr;
    DominatorTree domTree_;
    std::vector<std::string> errors_;

    void reportError(const std::string& msg);
    bool checkCFG();
    bool checkPhis();
    bool checkDominance();
    bool checkTypes();
};

} // namespace kernelforge::ir
