#pragma once

#include "Pass.h"

namespace kernelforge::opt {

class AlgebraicSimplificationPass : public Pass {
public:
    const char* getName() const override { return "AlgebraicSimplification"; }
    bool runOnFunction(ir::Function* func) override;
};

} // namespace kernelforge::opt
