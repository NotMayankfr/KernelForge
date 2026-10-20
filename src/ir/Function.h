#pragma once

#include "BasicBlock.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace kernelforge::ir {

class Module;

class Function : public Value {
public:
    Module* parentModule = nullptr;
    bool isKernel;
    std::shared_ptr<IRType> returnType;
    std::vector<std::unique_ptr<Argument>> arguments;
    std::vector<std::unique_ptr<BasicBlock>> basicBlocks;

    Function(std::string name, bool kernel, std::shared_ptr<IRType> retType, Module* m = nullptr);
    ~Function() override {
        dropAllReferences();
    }

    void dropAllReferences() {
        for (auto& bb : basicBlocks) {
            if (bb) {
                bb->dropAllReferences();
            }
        }
    }

    BasicBlock* getEntryBlock() const {
        return basicBlocks.empty() ? nullptr : basicBlocks.front().get();
    }

    BasicBlock* createBasicBlock(const std::string& name = "") {
        std::string bbName = name.empty() ? "bb" + std::to_string(basicBlocks.size()) : name;
        auto bb = std::make_unique<BasicBlock>(bbName, this);
        BasicBlock* ptr = bb.get();
        basicBlocks.push_back(std::move(bb));
        return ptr;
    }

    void removeBasicBlock(BasicBlock* bb) {
        if (bb) {
            bb->dropAllReferences();
        }
        basicBlocks.erase(std::remove_if(basicBlocks.begin(), basicBlocks.end(),
            [bb](const std::unique_ptr<BasicBlock>& b) { return b.get() == bb; }), basicBlocks.end());
    }

    size_t getInstructionCount() const {
        size_t count = 0;
        for (const auto& bb : basicBlocks) {
            count += bb->instructions.size();
        }
        return count;
    }
};

} // namespace kernelforge::ir
