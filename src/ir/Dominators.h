#pragma once

#include "BasicBlock.h"
#include "Function.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kernelforge::ir {

class DominatorTree {
public:
    void compute(Function* func);

    BasicBlock* getIDom(BasicBlock* bb) const;
    const std::vector<BasicBlock*>& getChildren(BasicBlock* bb) const;
    const std::unordered_set<BasicBlock*>& getDominanceFrontier(BasicBlock* bb) const;

    bool dominates(BasicBlock* a, BasicBlock* b) const;
    bool properlyDominates(BasicBlock* a, BasicBlock* b) const;
    bool dominates(const Instruction* def, const Instruction* use) const;

    const std::vector<BasicBlock*>& getReversePostOrder() const { return rpo_; }

    void dump() const;

private:
    Function* func_ = nullptr;
    std::unordered_map<BasicBlock*, BasicBlock*> idoms_;
    std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> children_;
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> df_;
    std::unordered_map<BasicBlock*, int> rpoNumbers_;
    std::vector<BasicBlock*> rpo_;

    // DFS numbering in dominator tree for O(1) dominance check
    std::unordered_map<BasicBlock*, int> domEntry_;
    std::unordered_map<BasicBlock*, int> domExit_;

    void computeRPO(BasicBlock* entry);
    void rpoDFS(BasicBlock* bb, std::unordered_set<BasicBlock*>& visited);
    BasicBlock* intersect(BasicBlock* b1, BasicBlock* b2);
    void computeDominanceFrontiers();
    void computeDomTreeDFS(BasicBlock* node, int& time);
};

} // namespace kernelforge::ir
