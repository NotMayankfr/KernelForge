#include "InstructionSelector.h"
#include <iostream>

namespace kernelforge::backend {

Register InstructionSelector::allocateVirtualReg(ir::IRType* type) {
    Register r;
    if (type->isFloat32()) {
        r.regClass = RegClass::Float;
        r.id = nextFloatReg_++;
    } else if (type->isBool()) {
        r.regClass = RegClass::Pred;
        r.id = nextPredReg_++;
    } else {
        r.regClass = RegClass::Int;
        r.id = nextIntReg_++;
    }
    r.isPhysical = false;
    return r;
}

Register InstructionSelector::getOrCreateReg(ir::Value* val, MachineBasicBlock* curMBB) {
    auto it = valToReg_.find(val);
    if (it != valToReg_.end()) {
        return it->second;
    }

    // If it's a constant, load into a register
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) {
        Register r = allocateVirtualReg(val->type.get());
        auto mov = std::make_unique<MachineInst>(MachineOpcode::MOV_B32);
        mov->addOperand(MachineOperand::createReg(r));
        mov->addOperand(MachineOperand::createImmInt(ci->value));
        curMBB->addInstruction(std::move(mov));
        valToReg_[val] = r;
        return r;
    }

    if (auto* cf = dynamic_cast<ir::ConstantFloat*>(val)) {
        Register r = allocateVirtualReg(val->type.get());
        auto mov = std::make_unique<MachineInst>(MachineOpcode::MOV_F32);
        mov->addOperand(MachineOperand::createReg(r));
        mov->addOperand(MachineOperand::createImmFloat(cf->value));
        curMBB->addInstruction(std::move(mov));
        valToReg_[val] = r;
        return r;
    }

    if (auto* cb = dynamic_cast<ir::ConstantBool*>(val)) {
        Register r = allocateVirtualReg(val->type.get());
        auto mov = std::make_unique<MachineInst>(MachineOpcode::MOV_B32);
        mov->addOperand(MachineOperand::createReg(r));
        mov->addOperand(MachineOperand::createImmBool(cb->value));
        curMBB->addInstruction(std::move(mov));
        valToReg_[val] = r;
        return r;
    }

    if (auto* gv = dynamic_cast<ir::GlobalVariable*>(val)) {
        Register r = allocateVirtualReg(ir::IRType::getInt32().get());
        auto mov = std::make_unique<MachineInst>(MachineOpcode::MOV_B32);
        mov->addOperand(MachineOperand::createReg(r));
        uint32_t offset = 0;
        auto it = globalOffsets_.find(gv);
        if (it != globalOffsets_.end()) {
            offset = it->second;
        }
        mov->addOperand(MachineOperand::createImmInt(static_cast<int32_t>(offset)));
        if (entryMBB_) {
            entryMBB_->addInstruction(std::move(mov));
        } else {
            curMBB->addInstruction(std::move(mov));
        }
        valToReg_[val] = r;
        return r;
    }

    Register r = allocateVirtualReg(val->type.get());
    valToReg_[val] = r;
    return r;
}

MachineOperand InstructionSelector::lowerOperand(ir::Value* val, MachineBasicBlock* curMBB) {
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) {
        return MachineOperand::createImmInt(ci->value);
    }
    if (auto* cf = dynamic_cast<ir::ConstantFloat*>(val)) {
        return MachineOperand::createImmFloat(cf->value);
    }
    if (auto* cb = dynamic_cast<ir::ConstantBool*>(val)) {
        return MachineOperand::createImmBool(cb->value);
    }
    return MachineOperand::createReg(getOrCreateReg(val, curMBB));
}

std::unique_ptr<MachineModule> InstructionSelector::select(ir::Module* irModule) {
    globalOffsets_.clear();
    uint32_t currentSharedOffset = 0;
    for (const auto& gv : irModule->globals) {
        if (gv->addressSpace == AddressSpace::Shared) {
            globalOffsets_[gv.get()] = currentSharedOffset;
            size_t count = gv->arraySize > 0 ? gv->arraySize : 1;
            currentSharedOffset += static_cast<uint32_t>(count * 4);
        }
    }

    auto mMod = std::make_unique<MachineModule>(irModule->name);
    for (const auto& func : irModule->functions) {
        mMod->functions.push_back(selectFunction(func.get()));
    }
    return mMod;
}

std::unique_ptr<MachineFunction> InstructionSelector::selectFunction(ir::Function* irFunc) {
    valToReg_.clear();
    nextIntReg_ = 0;
    nextFloatReg_ = 0;
    nextPredReg_ = 0;

    auto mFunc = std::make_unique<MachineFunction>(irFunc->name, irFunc->isKernel);
    for (const auto& arg : irFunc->arguments) {
        mFunc->paramNames.push_back(arg->name);
    }

    // 1. Create all MachineBasicBlocks
    std::unordered_map<ir::BasicBlock*, MachineBasicBlock*> bbMap;
    for (const auto& bbPtr : irFunc->basicBlocks) {
        MachineBasicBlock* mBB = mFunc->createBlock(bbPtr->name);
        bbMap[bbPtr.get()] = mBB;
    }
    entryMBB_ = mFunc->blocks.empty() ? nullptr : mFunc->blocks.front().get();

    // Connect MBB predecessors & successors
    for (const auto& bbPtr : irFunc->basicBlocks) {
        MachineBasicBlock* mBB = bbMap[bbPtr.get()];
        for (ir::BasicBlock* s : bbPtr->successors) {
            mBB->successors.push_back(bbMap[s]);
        }
        for (ir::BasicBlock* p : bbPtr->predecessors) {
            mBB->predecessors.push_back(bbMap[p]);
        }
    }

    // 2. Load parameters into registers in entry block from physical input registers (%r0, %r1, ... or %f0, %f1, ...)
    if (!irFunc->arguments.empty() && !mFunc->blocks.empty()) {
        MachineBasicBlock* entryMBB = mFunc->blocks.front().get();
        for (size_t i = 0; i < irFunc->arguments.size(); ++i) {
            ir::Argument* arg = irFunc->arguments[i].get();
            Register reg = allocateVirtualReg(arg->type.get());
            valToReg_[arg] = reg;

            Register paramPhysReg;
            paramPhysReg.isPhysical = true;
            paramPhysReg.id = static_cast<uint32_t>(i);
            paramPhysReg.regClass = arg->type->isFloat32() ? RegClass::Float : RegClass::Int;

            MachineOpcode movOp = arg->type->isFloat32() ? MachineOpcode::MOV_F32 : MachineOpcode::MOV_B32;
            auto mov = std::make_unique<MachineInst>(movOp);
            mov->addOperand(MachineOperand::createReg(reg));
            mov->addOperand(MachineOperand::createReg(paramPhysReg));
            entryMBB->addInstruction(std::move(mov));
        }
    }

    // 3. Out-of-SSA: Process Phi nodes
    // Map phi results to virtual registers
    for (const auto& bbPtr : irFunc->basicBlocks) {
        for (const auto& instPtr : bbPtr->instructions) {
            if (auto* phi = dynamic_cast<ir::PhiInst*>(instPtr.get())) {
                Register phiReg = allocateVirtualReg(phi->type.get());
                valToReg_[phi] = phiReg;
            }
        }
    }

    // Lower non-phi instructions in each block
    for (const auto& bbPtr : irFunc->basicBlocks) {
        ir::BasicBlock* irBB = bbPtr.get();
        MachineBasicBlock* mBB = bbMap[irBB];
        lowerBlock(irBB, mBB);
    }

    // Insert phi copies into predecessors before their terminator
    for (const auto& bbPtr : irFunc->basicBlocks) {
        for (const auto& instPtr : bbPtr->instructions) {
            if (auto* phi = dynamic_cast<ir::PhiInst*>(instPtr.get())) {
                Register phiReg = valToReg_[phi];
                for (size_t i = 0; i < phi->numIncoming(); ++i) {
                    ir::Value* incomingVal = phi->getIncomingValue(i);
                    ir::BasicBlock* predBB = phi->getIncomingBlock(i);
                    MachineBasicBlock* predMBB = bbMap[predBB];

                    MachineOpcode movOp = phi->type->isFloat32() ? MachineOpcode::MOV_F32 : MachineOpcode::MOV_B32;
                    auto copyInst = std::make_unique<MachineInst>(movOp);
                    copyInst->addOperand(MachineOperand::createReg(phiReg));
                    copyInst->addOperand(lowerOperand(incomingVal, predMBB));

                    // Insert before terminator if present
                    if (!predMBB->instructions.empty() &&
                        (predMBB->instructions.back()->opcode == MachineOpcode::BRA ||
                         predMBB->instructions.back()->opcode == MachineOpcode::RET)) {
                        predMBB->instructions.insert(predMBB->instructions.end() - 1, std::move(copyInst));
                    } else {
                        predMBB->addInstruction(std::move(copyInst));
                    }
                }
            }
        }
    }

    return mFunc;
}

void InstructionSelector::lowerBlock(ir::BasicBlock* irBB, MachineBasicBlock* mBB) {
    for (const auto& instPtr : irBB->instructions) {
        ir::Instruction* inst = instPtr.get();

        if (dynamic_cast<ir::PhiInst*>(inst) || dynamic_cast<ir::AllocaInst*>(inst)) {
            // Handled separately
            continue;
        }

        switch (inst->opcode) {
            case ir::Opcode::Add:
            case ir::Opcode::Sub:
            case ir::Opcode::Mul:
            case ir::Opcode::SDiv:
            case ir::Opcode::SRem:
            case ir::Opcode::Shl:
            case ir::Opcode::AShr:
            case ir::Opcode::And:
            case ir::Opcode::Or:
            case ir::Opcode::Xor:
            case ir::Opcode::FAdd:
            case ir::Opcode::FSub:
            case ir::Opcode::FMul:
            case ir::Opcode::FDiv: {
                Register dst = getOrCreateReg(inst, mBB);
                MachineOperand lhs = MachineOperand::createReg(getOrCreateReg(inst->getOperand(0), mBB));
                MachineOperand rhs = lowerOperand(inst->getOperand(1), mBB);

                MachineOpcode mOp;
                switch (inst->opcode) {
                    case ir::Opcode::Add:  mOp = MachineOpcode::ADD_S32; break;
                    case ir::Opcode::Sub:  mOp = MachineOpcode::SUB_S32; break;
                    case ir::Opcode::Mul:  mOp = MachineOpcode::MUL_S32; break;
                    case ir::Opcode::SDiv: mOp = MachineOpcode::DIV_S32; break;
                    case ir::Opcode::SRem: mOp = MachineOpcode::REM_S32; break;
                    case ir::Opcode::Shl:  mOp = MachineOpcode::SHL_B32; break;
                    case ir::Opcode::AShr: mOp = MachineOpcode::SHR_S32; break;
                    case ir::Opcode::And:  mOp = MachineOpcode::AND_B32; break;
                    case ir::Opcode::Or:   mOp = MachineOpcode::OR_B32; break;
                    case ir::Opcode::Xor:  mOp = MachineOpcode::XOR_B32; break;
                    case ir::Opcode::FAdd: mOp = MachineOpcode::ADD_F32; break;
                    case ir::Opcode::FSub: mOp = MachineOpcode::SUB_F32; break;
                    case ir::Opcode::FMul: mOp = MachineOpcode::MUL_F32; break;
                    case ir::Opcode::FDiv: mOp = MachineOpcode::DIV_F32; break;
                    default: mOp = MachineOpcode::ADD_S32; break;
                }

                auto mInst = std::make_unique<MachineInst>(mOp);
                mInst->addOperand(MachineOperand::createReg(dst));
                mInst->addOperand(lhs);
                mInst->addOperand(rhs);
                mBB->addInstruction(std::move(mInst));
                break;
            }
            case ir::Opcode::ICmp: {
                auto* cmp = static_cast<ir::ICmpInst*>(inst);
                Register dst = getOrCreateReg(inst, mBB);
                MachineOperand lhs = MachineOperand::createReg(getOrCreateReg(cmp->getLHS(), mBB));
                MachineOperand rhs = lowerOperand(cmp->getRHS(), mBB);

                MachineOpcode mOp;
                switch (cmp->predicate) {
                    case ir::CmpPredicate::EQ:  mOp = MachineOpcode::SETP_EQ_S32; break;
                    case ir::CmpPredicate::NE:  mOp = MachineOpcode::SETP_NE_S32; break;
                    case ir::CmpPredicate::SLT: mOp = MachineOpcode::SETP_LT_S32; break;
                    case ir::CmpPredicate::SLE: mOp = MachineOpcode::SETP_LE_S32; break;
                    case ir::CmpPredicate::SGT: mOp = MachineOpcode::SETP_GT_S32; break;
                    case ir::CmpPredicate::SGE: mOp = MachineOpcode::SETP_GE_S32; break;
                    default: mOp = MachineOpcode::SETP_EQ_S32; break;
                }

                auto mInst = std::make_unique<MachineInst>(mOp);
                mInst->addOperand(MachineOperand::createReg(dst));
                mInst->addOperand(lhs);
                mInst->addOperand(rhs);
                mBB->addInstruction(std::move(mInst));
                break;
            }
            case ir::Opcode::FCmp: {
                auto* cmp = static_cast<ir::FCmpInst*>(inst);
                Register dst = getOrCreateReg(inst, mBB);
                MachineOperand lhs = MachineOperand::createReg(getOrCreateReg(cmp->getLHS(), mBB));
                MachineOperand rhs = lowerOperand(cmp->getRHS(), mBB);

                MachineOpcode mOp;
                switch (cmp->predicate) {
                    case ir::CmpPredicate::EQ:  mOp = MachineOpcode::SETP_EQ_F32; break;
                    case ir::CmpPredicate::NE:  mOp = MachineOpcode::SETP_NE_F32; break;
                    case ir::CmpPredicate::OLT: mOp = MachineOpcode::SETP_LT_F32; break;
                    case ir::CmpPredicate::OLE: mOp = MachineOpcode::SETP_LE_F32; break;
                    case ir::CmpPredicate::OGT: mOp = MachineOpcode::SETP_GT_F32; break;
                    case ir::CmpPredicate::OGE: mOp = MachineOpcode::SETP_GE_F32; break;
                    default: mOp = MachineOpcode::SETP_EQ_F32; break;
                }

                auto mInst = std::make_unique<MachineInst>(mOp);
                mInst->addOperand(MachineOperand::createReg(dst));
                mInst->addOperand(lhs);
                mInst->addOperand(rhs);
                mBB->addInstruction(std::move(mInst));
                break;
            }
            case ir::Opcode::Load: {
                auto* ld = static_cast<ir::LoadInst*>(inst);
                Register dst = getOrCreateReg(inst, mBB);
                Register ptrReg = getOrCreateReg(ld->getPointer(), mBB);

                MachineOpcode mOp;
                bool isFloat = ld->type->isFloat32();
                switch (ld->getAddressSpace()) {
                    case AddressSpace::Shared:
                        mOp = isFloat ? MachineOpcode::LD_SHARED_F32 : MachineOpcode::LD_SHARED_S32;
                        break;
                    case AddressSpace::Local:
                        mOp = isFloat ? MachineOpcode::LD_LOCAL_F32 : MachineOpcode::LD_LOCAL_S32;
                        break;
                    default:
                        mOp = isFloat ? MachineOpcode::LD_GLOBAL_F32 : MachineOpcode::LD_GLOBAL_S32;
                        break;
                }

                auto mInst = std::make_unique<MachineInst>(mOp);
                mInst->addOperand(MachineOperand::createReg(dst));
                mInst->addOperand(MachineOperand::createMem(ptrReg));
                mBB->addInstruction(std::move(mInst));
                break;
            }
            case ir::Opcode::Store: {
                auto* st = static_cast<ir::StoreInst*>(inst);
                Register valReg = getOrCreateReg(st->getValue(), mBB);
                Register ptrReg = getOrCreateReg(st->getPointer(), mBB);

                MachineOpcode mOp;
                bool isFloat = st->getValue()->type->isFloat32();
                switch (st->getAddressSpace()) {
                    case AddressSpace::Shared:
                        mOp = isFloat ? MachineOpcode::ST_SHARED_F32 : MachineOpcode::ST_SHARED_S32;
                        break;
                    case AddressSpace::Local:
                        mOp = isFloat ? MachineOpcode::ST_LOCAL_F32 : MachineOpcode::ST_LOCAL_S32;
                        break;
                    default:
                        mOp = isFloat ? MachineOpcode::ST_GLOBAL_F32 : MachineOpcode::ST_GLOBAL_S32;
                        break;
                }

                auto mInst = std::make_unique<MachineInst>(mOp);
                mInst->addOperand(MachineOperand::createMem(ptrReg));
                mInst->addOperand(MachineOperand::createReg(valReg));
                mBB->addInstruction(std::move(mInst));
                break;
            }
            case ir::Opcode::GetElementPtr: {
                auto* gep = static_cast<ir::GetElementPtrInst*>(inst);
                Register dst = getOrCreateReg(inst, mBB);
                Register baseReg = getOrCreateReg(gep->getPointer(), mBB);
                Register idxReg = getOrCreateReg(gep->getIndex(), mBB);

                // mul.s32 %off, %idx, 4
                Register offReg;
                offReg.regClass = RegClass::Int;
                offReg.id = nextIntReg_++;
                offReg.isPhysical = false;

                auto mulInst = std::make_unique<MachineInst>(MachineOpcode::MUL_S32);
                mulInst->addOperand(MachineOperand::createReg(offReg));
                mulInst->addOperand(MachineOperand::createReg(idxReg));
                mulInst->addOperand(MachineOperand::createImmInt(4)); // 4 bytes per element
                mBB->addInstruction(std::move(mulInst));

                // add.s32 %dst, %base, %off
                auto addInst = std::make_unique<MachineInst>(MachineOpcode::ADD_S32);
                addInst->addOperand(MachineOperand::createReg(dst));
                addInst->addOperand(MachineOperand::createReg(baseReg));
                addInst->addOperand(MachineOperand::createReg(offReg));
                mBB->addInstruction(std::move(addInst));
                break;
            }
            case ir::Opcode::GetSpecial: {
                auto* sp = static_cast<ir::GetSpecialInst*>(inst);
                Register dst = getOrCreateReg(inst, mBB);

                Register specialReg;
                specialReg.regClass = RegClass::Special;
                specialReg.id = 0;
                specialReg.isPhysical = true;

                if (sp->specialReg == "threadIdx.x") specialReg.specialName = "tid.x";
                else if (sp->specialReg == "threadIdx.y") specialReg.specialName = "tid.y";
                else if (sp->specialReg == "threadIdx.z") specialReg.specialName = "tid.z";
                else if (sp->specialReg == "blockIdx.x")  specialReg.specialName = "ctaid.x";
                else if (sp->specialReg == "blockIdx.y")  specialReg.specialName = "ctaid.y";
                else if (sp->specialReg == "blockIdx.z")  specialReg.specialName = "ctaid.z";
                else if (sp->specialReg == "blockDim.x")  specialReg.specialName = "ntid.x";
                else if (sp->specialReg == "blockDim.y")  specialReg.specialName = "ntid.y";
                else if (sp->specialReg == "blockDim.z")  specialReg.specialName = "ntid.z";
                else if (sp->specialReg == "gridDim.x")   specialReg.specialName = "nctaid.x";
                else specialReg.specialName = sp->specialReg;

                auto mov = std::make_unique<MachineInst>(MachineOpcode::MOV_B32);
                mov->addOperand(MachineOperand::createReg(dst));
                mov->addOperand(MachineOperand::createReg(specialReg));
                mBB->addInstruction(std::move(mov));
                break;
            }
            case ir::Opcode::Sync: {
                auto sync = std::make_unique<MachineInst>(MachineOpcode::BAR_SYNC);
                sync->addOperand(MachineOperand::createImmInt(0));
                mBB->addInstruction(std::move(sync));
                break;
            }
            case ir::Opcode::SIToFP: {
                Register dst = getOrCreateReg(inst, mBB);
                Register src = getOrCreateReg(inst->getOperand(0), mBB);
                auto cvt = std::make_unique<MachineInst>(MachineOpcode::CVT_F32_S32);
                cvt->addOperand(MachineOperand::createReg(dst));
                cvt->addOperand(MachineOperand::createReg(src));
                mBB->addInstruction(std::move(cvt));
                break;
            }
            case ir::Opcode::FPToSI: {
                Register dst = getOrCreateReg(inst, mBB);
                Register src = getOrCreateReg(inst->getOperand(0), mBB);
                auto cvt = std::make_unique<MachineInst>(MachineOpcode::CVT_S32_F32);
                cvt->addOperand(MachineOperand::createReg(dst));
                cvt->addOperand(MachineOperand::createReg(src));
                mBB->addInstruction(std::move(cvt));
                break;
            }
            case ir::Opcode::Branch: {
                auto* br = static_cast<ir::BranchInst*>(inst);
                Register condReg = getOrCreateReg(br->getCondition(), mBB);

                // @%p0 bra $trueTarget;
                auto braTrue = std::make_unique<MachineInst>(MachineOpcode::BRA);
                braTrue->setPredicate(condReg, false);
                braTrue->addOperand(MachineOperand::createLabel(br->getTrueTarget()->name));
                mBB->addInstruction(std::move(braTrue));

                // bra $falseTarget;
                auto braFalse = std::make_unique<MachineInst>(MachineOpcode::BRA);
                braFalse->addOperand(MachineOperand::createLabel(br->getFalseTarget()->name));
                mBB->addInstruction(std::move(braFalse));
                break;
            }
            case ir::Opcode::Jump: {
                auto* jmp = static_cast<ir::JumpInst*>(inst);
                auto bra = std::make_unique<MachineInst>(MachineOpcode::BRA);
                bra->addOperand(MachineOperand::createLabel(jmp->getTarget()->name));
                mBB->addInstruction(std::move(bra));
                break;
            }
            case ir::Opcode::Return: {
                auto* ret = static_cast<ir::ReturnInst*>(inst);
                if (ret->getReturnValue()) {
                    // Copy return value to %r0 or %f0
                    Register retValReg = getOrCreateReg(ret->getReturnValue(), mBB);
                    Register outReg;
                    outReg.isPhysical = true;
                    outReg.id = 0;
                    outReg.regClass = ret->getReturnValue()->type->isFloat32() ? RegClass::Float : RegClass::Int;

                    auto mov = std::make_unique<MachineInst>(
                        outReg.regClass == RegClass::Float ? MachineOpcode::MOV_F32 : MachineOpcode::MOV_B32);
                    mov->addOperand(MachineOperand::createReg(outReg));
                    mov->addOperand(MachineOperand::createReg(retValReg));
                    mBB->addInstruction(std::move(mov));
                }
                auto retInst = std::make_unique<MachineInst>(MachineOpcode::RET);
                mBB->addInstruction(std::move(retInst));
                break;
            }
            case ir::Opcode::Call: {
                auto* call = static_cast<ir::CallInst*>(inst);
                // Copy arguments to %r0, %r1, ... or %f0, %f1, ...
                for (size_t i = 0; i < call->numArgs(); ++i) {
                    ir::Value* argVal = call->getArg(i);
                    Register argReg = getOrCreateReg(argVal, mBB);
                    Register inPhysReg;
                    inPhysReg.isPhysical = true;
                    inPhysReg.id = static_cast<uint32_t>(i);
                    inPhysReg.regClass = argVal->type->isFloat32() ? RegClass::Float : RegClass::Int;

                    MachineOpcode movOp = inPhysReg.regClass == RegClass::Float ? MachineOpcode::MOV_F32 : MachineOpcode::MOV_B32;
                    auto mov = std::make_unique<MachineInst>(movOp);
                    mov->addOperand(MachineOperand::createReg(inPhysReg));
                    mov->addOperand(MachineOperand::createReg(argReg));
                    mBB->addInstruction(std::move(mov));
                }

                auto mCall = std::make_unique<MachineInst>(MachineOpcode::CALL);
                mCall->addOperand(MachineOperand::createLabel(call->callee));
                mBB->addInstruction(std::move(mCall));

                // If function returns value, get result from %r0 or %f0
                if (!call->type->isVoid()) {
                    Register dst = getOrCreateReg(call, mBB);
                    Register retReg;
                    retReg.isPhysical = true;
                    retReg.id = 0;
                    retReg.regClass = call->type->isFloat32() ? RegClass::Float : RegClass::Int;

                    auto mov = std::make_unique<MachineInst>(
                        retReg.regClass == RegClass::Float ? MachineOpcode::MOV_F32 : MachineOpcode::MOV_B32);
                    mov->addOperand(MachineOperand::createReg(dst));
                    mov->addOperand(MachineOperand::createReg(retReg));
                    mBB->addInstruction(std::move(mov));
                }
                break;
            }
            default:
                break;
        }
    }
}

} // namespace kernelforge::backend
