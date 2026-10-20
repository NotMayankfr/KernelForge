#pragma once

#include "Pass.h"

namespace kernelforge::opt {

class StrengthReductionPass : public Pass {
public:
    const char* getName() const override { return "StrengthReduction"; }
    bool runOnFunction(ir::Function* func) override;
};

} // namespace kernelforge::opt
