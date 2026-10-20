#pragma once

#include "Instruction.h"
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

namespace kernelforge::ir {

class Function;

class BasicBlock : public Value {
public:
    Function* parent = nullptr;
    std::vector<std::unique_ptr<Instruction>> instructions;
    std::vector<BasicBlock*> predecessors;
    std::vector<BasicBlock*> successors;

    BasicBlock(std::string name, Function* p = nullptr);
    ~BasicBlock() override {
        dropAllReferences();
    }

    void dropAllReferences() {
        for (auto& inst : instructions) {
            if (inst) {
                inst->dropAllReferences();
            }
        }
    }

    Instruction* getTerminator() const {
        if (instructions.empty()) return nullptr;
        if (instructions.back()->isTerminator()) return instructions.back().get();
        return nullptr;
    }

    bool isTerminated() const {
        return getTerminator() != nullptr;
    }

    void addInstruction(std::unique_ptr<Instruction> inst) {
        inst->parent = this;
        instructions.push_back(std::move(inst));
    }

    void insertInstructionBefore(Instruction* beforeInst, std::unique_ptr<Instruction> newInst) {
        newInst->parent = this;
        auto it = std::find_if(instructions.begin(), instructions.end(),
            [beforeInst](const std::unique_ptr<Instruction>& i) { return i.get() == beforeInst; });
        instructions.insert(it, std::move(newInst));
    }

    void insertInstructionAtBeginning(std::unique_ptr<Instruction> inst) {
        inst->parent = this;
        instructions.insert(instructions.begin(), std::move(inst));
    }

    std::unique_ptr<Instruction> removeInstruction(Instruction* inst) {
        auto it = std::find_if(instructions.begin(), instructions.end(),
            [inst](const std::unique_ptr<Instruction>& i) { return i.get() == inst; });
        if (it != instructions.end()) {
            std::unique_ptr<Instruction> ret = std::move(*it);
            instructions.erase(it);
            ret->parent = nullptr;
            return ret;
        }
        return nullptr;
    }

    void addPredecessor(BasicBlock* bb) {
        if (std::find(predecessors.begin(), predecessors.end(), bb) == predecessors.end()) {
            predecessors.push_back(bb);
        }
    }

    void removePredecessor(BasicBlock* bb) {
        predecessors.erase(std::remove(predecessors.begin(), predecessors.end(), bb), predecessors.end());
    }

    void addSuccessor(BasicBlock* bb) {
        if (std::find(successors.begin(), successors.end(), bb) == successors.end()) {
            successors.push_back(bb);
        }
    }

    void removeSuccessor(BasicBlock* bb) {
        successors.erase(std::remove(successors.begin(), successors.end(), bb), successors.end());
    }

    void replaceSuccessor(BasicBlock* oldBB, BasicBlock* newBB) {
        for (auto& s : successors) {
            if (s == oldBB) s = newBB;
        }
        auto* term = getTerminator();
        if (term) {
            if (auto* br = dynamic_cast<BranchInst*>(term)) {
                if (br->trueTarget == oldBB) br->trueTarget = newBB;
                if (br->falseTarget == oldBB) br->falseTarget = newBB;
            } else if (auto* jmp = dynamic_cast<JumpInst*>(term)) {
                if (jmp->target == oldBB) jmp->target = newBB;
            }
        }
    }
};

} // namespace kernelforge::ir
