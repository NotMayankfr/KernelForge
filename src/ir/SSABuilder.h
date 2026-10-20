#pragma once

#include "Function.h"
#include "Dominators.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kernelforge::ir {

class SSABuilder {
public:
    void run(Function* func);

private:
    Function* func_ = nullptr;
    DominatorTree domTree_;

    std::vector<AllocaInst*> candidateAllocas_;
    std::unordered_map<AllocaInst*, std::unordered_set<BasicBlock*>> defBlocks_;
    std::unordered_map<AllocaInst*, std::unordered_set<BasicBlock*>> phiBlocks_;
    std::unordered_map<BasicBlock*, std::unordered_map<AllocaInst*, PhiInst*>> blockPhiMap_;

    std::unordered_map<AllocaInst*, std::vector<Value*>> valueStacks_;
    std::unordered_set<Instruction*> instructionsToRemove_;

    void findPromotableAllocas();
    void computeIDF();
    void insertPhiNodes();
    void rename(BasicBlock* bb);
    Value* getDefaultValue(AllocaInst* alloca);
};

} // namespace kernelforge::ir
