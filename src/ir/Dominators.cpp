#include "Dominators.h"
#include <iostream>
#include <algorithm>

namespace kernelforge::ir {

void DominatorTree::compute(Function* func) {
    func_ = func;
    idoms_.clear();
    children_.clear();
    df_.clear();
    rpoNumbers_.clear();
    rpo_.clear();
    domEntry_.clear();
    domExit_.clear();

    BasicBlock* entry = func->getEntryBlock();
    if (!entry) return;

    // 1. Compute Reverse Post-Order (RPO)
    computeRPO(entry);

    // 2. Iterative Dominator algorithm (Cooper-Harvey-Kennedy)
    idoms_[entry] = entry;

    bool changed = true;
    while (changed) {
        changed = false;
        // Process blocks in RPO order, skipping entry (index 0)
        for (size_t i = 1; i < rpo_.size(); ++i) {
            BasicBlock* b = rpo_[i];

            // Find first processed predecessor
            BasicBlock* newIDom = nullptr;
            for (BasicBlock* p : b->predecessors) {
                if (idoms_.find(p) != idoms_.end()) {
                    newIDom = p;
                    break;
                }
            }

            if (!newIDom) continue;

            for (BasicBlock* p : b->predecessors) {
                if (p != newIDom && idoms_.find(p) != idoms_.end()) {
                    newIDom = intersect(p, newIDom);
                }
            }

            if (idoms_.find(b) == idoms_.end() || idoms_[b] != newIDom) {
                idoms_[b] = newIDom;
                changed = true;
            }
        }
    }

    // Fix entry's idom to nullptr
    idoms_[entry] = nullptr;

    // Build children map
    for (const auto& [b, idom] : idoms_) {
        if (idom) {
            children_[idom].push_back(b);
        }
    }

    // 3. Compute dominance tree DFS entry/exit times
    int time = 0;
    computeDomTreeDFS(entry, time);

    // 4. Compute Dominance Frontiers
    computeDominanceFrontiers();
}

void DominatorTree::computeRPO(BasicBlock* entry) {
    std::unordered_set<BasicBlock*> visited;
    rpoDFS(entry, visited);
    std::reverse(rpo_.begin(), rpo_.end());
    for (size_t i = 0; i < rpo_.size(); ++i) {
        rpoNumbers_[rpo_[i]] = static_cast<int>(i);
    }
}

void DominatorTree::rpoDFS(BasicBlock* bb, std::unordered_set<BasicBlock*>& visited) {
    visited.insert(bb);
    for (BasicBlock* succ : bb->successors) {
        if (visited.find(succ) == visited.end()) {
            rpoDFS(succ, visited);
        }
    }
    rpo_.push_back(bb);
}

BasicBlock* DominatorTree::intersect(BasicBlock* b1, BasicBlock* b2) {
    BasicBlock* finger1 = b1;
    BasicBlock* finger2 = b2;

    while (finger1 != finger2) {
        while (rpoNumbers_[finger1] > rpoNumbers_[finger2]) {
            finger1 = idoms_[finger1];
        }
        while (rpoNumbers_[finger2] > rpoNumbers_[finger1]) {
            finger2 = idoms_[finger2];
        }
    }

    return finger1;
}

void DominatorTree::computeDomTreeDFS(BasicBlock* node, int& time) {
    domEntry_[node] = ++time;
    if (children_.find(node) != children_.end()) {
        for (BasicBlock* child : children_[node]) {
            computeDomTreeDFS(child, time);
        }
    }
    domExit_[node] = ++time;
}

void DominatorTree::computeDominanceFrontiers() {
    for (const auto& bbPtr : func_->basicBlocks) {
        BasicBlock* b = bbPtr.get();
        if (b->predecessors.size() >= 2) {
            for (BasicBlock* p : b->predecessors) {
                BasicBlock* runner = p;
                while (runner && runner != idoms_[b]) {
                    df_[runner].insert(b);
                    runner = idoms_[runner];
                }
            }
        }
    }
}

BasicBlock* DominatorTree::getIDom(BasicBlock* bb) const {
    auto it = idoms_.find(bb);
    return (it != idoms_.end()) ? it->second : nullptr;
}

const std::vector<BasicBlock*>& DominatorTree::getChildren(BasicBlock* bb) const {
    static const std::vector<BasicBlock*> emptyList;
    auto it = children_.find(bb);
    return (it != children_.end()) ? it->second : emptyList;
}

const std::unordered_set<BasicBlock*>& DominatorTree::getDominanceFrontier(BasicBlock* bb) const {
    static const std::unordered_set<BasicBlock*> emptySet;
    auto it = df_.find(bb);
    return (it != df_.end()) ? it->second : emptySet;
}

bool DominatorTree::dominates(BasicBlock* a, BasicBlock* b) const {
    if (a == b) return true;
    auto itAEntry = domEntry_.find(a);
    auto itBEntry = domEntry_.find(b);
    if (itAEntry == domEntry_.end() || itBEntry == domEntry_.end()) return false;
    return (itAEntry->second <= itBEntry->second) && (domExit_.at(a) >= domExit_.at(b));
}

bool DominatorTree::properlyDominates(BasicBlock* a, BasicBlock* b) const {
    return a != b && dominates(a, b);
}

bool DominatorTree::dominates(const Instruction* def, const Instruction* use) const {
    if (!def || !use) return false;
    BasicBlock* defBB = def->parent;
    BasicBlock* useBB = use->parent;
    if (!defBB || !useBB) return false;

    // Special handling for Phi instruction:
    // A use in a Phi instruction takes place along the CFG edge from the corresponding incoming block!
    if (auto* phi = dynamic_cast<const PhiInst*>(use)) {
        for (size_t i = 0; i < phi->numIncoming(); ++i) {
            if (phi->getIncomingValue(i) == def) {
                BasicBlock* incomingBB = phi->getIncomingBlock(i);
                if (!dominates(defBB, incomingBB)) {
                    return false;
                }
            }
        }
        return true;
    }

    if (defBB != useBB) {
        return dominates(defBB, useBB);
    }

    // In the same basic block: def must appear before use
    for (const auto& instPtr : defBB->instructions) {
        if (instPtr.get() == def) return true;
        if (instPtr.get() == use) return false;
    }

    return false;
}

void DominatorTree::dump() const {
    std::cout << "Dominator Tree for function: " << (func_ ? func_->name : "null") << "\n";
    for (BasicBlock* bb : rpo_) {
        std::cout << "  " << bb->name << " [idom: " << (idoms_.at(bb) ? idoms_.at(bb)->name : "none") << "] DF: { ";
        auto it = df_.find(bb);
        if (it != df_.end()) {
            for (BasicBlock* f : it->second) {
                std::cout << f->name << " ";
            }
        }
        std::cout << "}\n";
    }
}

} // namespace kernelforge::ir
