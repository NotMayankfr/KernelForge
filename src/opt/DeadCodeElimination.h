#pragma once

#include "Pass.h"

namespace kernelforge::opt {

class DeadCodeEliminationPass : public Pass {
public:
    const char* getName() const override { return "DeadCodeElimination"; }
    bool runOnFunction(ir::Function* func) override;

private:
    bool eliminateDeadInstructions(ir::Function* func);
    bool eliminateUnreachableBlocks(ir::Function* func);
};

} // namespace kernelforge::opt
