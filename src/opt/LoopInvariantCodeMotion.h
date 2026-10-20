#pragma once

#include "Pass.h"
#include "ir/Dominators.h"
#include <unordered_set>
#include <vector>

namespace kernelforge::opt {

class LoopInvariantCodeMotionPass : public Pass {
public:
    const char* getName() const override { return "LoopInvariantCodeMotion"; }
    bool runOnFunction(ir::Function* func) override;

private:
    struct Loop {
        ir::BasicBlock* header;
        ir::BasicBlock* preheader;
        std::unordered_set<ir::BasicBlock*> blocks;
    };

    std::vector<Loop> findLoops(ir::Function* func, const ir::DominatorTree& domTree);
    bool hoistLoopInvariants(Loop& loop);
};

} // namespace kernelforge::opt
