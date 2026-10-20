#pragma once

#include "Pass.h"

namespace kernelforge::opt {

class ConstantFoldingPass : public Pass {
public:
    const char* getName() const override { return "ConstantFolding"; }
    bool runOnFunction(ir::Function* func) override;
};

} // namespace kernelforge::opt
