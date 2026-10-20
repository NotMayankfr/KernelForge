#pragma once

#include "VirtualISA.h"
#include "ir/Module.h"
#include <unordered_map>

namespace kernelforge::backend {

class InstructionSelector {
public:
    std::unique_ptr<MachineModule> select(ir::Module* irModule);
    std::unique_ptr<MachineFunction> selectFunction(ir::Function* irFunc);

private:
    std::unordered_map<ir::Value*, Register> valToReg_;
    std::unordered_map<ir::GlobalVariable*, uint32_t> globalOffsets_;
    MachineBasicBlock* entryMBB_ = nullptr;
    int nextIntReg_ = 0;
    int nextFloatReg_ = 0;
    int nextPredReg_ = 0;

    Register allocateVirtualReg(ir::IRType* type);
    Register getOrCreateReg(ir::Value* val, MachineBasicBlock* curMBB);
    MachineOperand lowerOperand(ir::Value* val, MachineBasicBlock* curMBB);

    void eliminatePhis(ir::Function* irFunc, MachineFunction* mFunc);
    void lowerBlock(ir::BasicBlock* irBB, MachineBasicBlock* mBB);
};

} // namespace kernelforge::backend
