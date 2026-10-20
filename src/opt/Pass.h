#pragma once

#include "ir/Function.h"
#include <string>

namespace kernelforge::opt {

class Pass {
public:
    virtual ~Pass() = default;
    virtual const char* getName() const = 0;
    virtual bool runOnFunction(ir::Function* func) = 0;
};

} // namespace kernelforge::opt
