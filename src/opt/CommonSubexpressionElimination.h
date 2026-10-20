#pragma once

#include "Pass.h"
#include "ir/Dominators.h"

namespace kernelforge::opt {

class CommonSubexpressionEliminationPass : public Pass {
public:
    const char* getName() const override { return "CommonSubexpressionElimination"; }
    bool runOnFunction(ir::Function* func) override;
};

} // namespace kernelforge::opt
