#include "Instruction.h"
#include "BasicBlock.h"
#include "Function.h"
#include "Module.h"
#include <iostream>

namespace kernelforge::ir {

void Value::replaceAllUsesWith(Value* newVal) {
    if (this == newVal) return;
    auto usesCopy = uses;
    for (const auto& u : usesCopy) {
        u.user->setOperand(u.operandIndex, newVal);
    }
    uses.clear();
}

void Instruction::dropAllReferences() {
    for (size_t i = 0; i < operands.size(); ++i) {
        if (operands[i]) {
            operands[i]->removeUse(this, i);
        }
    }
    operands.clear();
}

Instruction::~Instruction() {
    dropAllReferences();
}

void Instruction::setOperand(size_t idx, Value* newVal) {
    if (idx < operands.size() && operands[idx]) {
        operands[idx]->removeUse(this, idx);
    }
    if (idx >= operands.size()) {
        operands.resize(idx + 1, nullptr);
    }
    operands[idx] = newVal;
    if (newVal) {
        newVal->addUse(this, idx);
    }
}

const char* opcodeToString(Opcode op) {
    switch (op) {
        case Opcode::Alloca: return "alloca";
        case Opcode::Load: return "load";
        case Opcode::Store: return "store";
        case Opcode::GetElementPtr: return "gep";
        case Opcode::Add: return "add";
        case Opcode::Sub: return "sub";
        case Opcode::Mul: return "mul";
        case Opcode::SDiv: return "sdiv";
        case Opcode::SRem: return "srem";
        case Opcode::Shl: return "shl";
        case Opcode::AShr: return "ashr";
        case Opcode::And: return "and";
        case Opcode::Or: return "or";
        case Opcode::Xor: return "xor";
        case Opcode::FAdd: return "fadd";
        case Opcode::FSub: return "fsub";
        case Opcode::FMul: return "fmul";
        case Opcode::FDiv: return "fdiv";
        case Opcode::ICmp: return "icmp";
        case Opcode::FCmp: return "fcmp";
        case Opcode::Phi: return "phi";
        case Opcode::Select: return "select";
        case Opcode::SIToFP: return "sitofp";
        case Opcode::FPToSI: return "fptosi";
        case Opcode::BitCast: return "bitcast";
        case Opcode::GetSpecial: return "get_special";
        case Opcode::Sync: return "sync";
        case Opcode::Branch: return "br";
        case Opcode::Jump: return "jmp";
        case Opcode::Return: return "ret";
        case Opcode::Call: return "call";
    }
    return "unknown";
}

const char* predicateToString(CmpPredicate pred) {
    switch (pred) {
        case CmpPredicate::EQ: return "eq";
        case CmpPredicate::NE: return "ne";
        case CmpPredicate::SLT: return "slt";
        case CmpPredicate::SLE: return "sle";
        case CmpPredicate::SGT: return "sgt";
        case CmpPredicate::SGE: return "sge";
        case CmpPredicate::OLT: return "olt";
        case CmpPredicate::OLE: return "ole";
        case CmpPredicate::OGT: return "ogt";
        case CmpPredicate::OGE: return "oge";
    }
    return "unknown";
}

// Memory instructions
LoadInst::LoadInst(Value* ptr, std::string name, BasicBlock* parent)
    : Instruction(Opcode::Load, nullptr, std::move(name), parent) {
    setOperand(0, ptr);
    if (ptr && ptr->type && ptr->type->isPointer()) {
        auto* pt = static_cast<PointerIRType*>(ptr->type.get());
        type = pt->pointee;
    } else {
        type = IRType::getInt32();
    }
}

AddressSpace LoadInst::getAddressSpace() const {
    Value* ptr = getPointer();
    if (ptr && ptr->type && ptr->type->isPointer()) {
        return static_cast<PointerIRType*>(ptr->type.get())->addressSpace;
    }
    return AddressSpace::Generic;
}

StoreInst::StoreInst(Value* val, Value* ptr, BasicBlock* parent)
    : Instruction(Opcode::Store, IRType::getVoid(), "", parent) {
    setOperand(0, val);
    setOperand(1, ptr);
}

AddressSpace StoreInst::getAddressSpace() const {
    Value* ptr = getPointer();
    if (ptr && ptr->type && ptr->type->isPointer()) {
        return static_cast<PointerIRType*>(ptr->type.get())->addressSpace;
    }
    return AddressSpace::Generic;
}

GetElementPtrInst::GetElementPtrInst(Value* ptr, Value* index, std::string name, BasicBlock* parent)
    : Instruction(Opcode::GetElementPtr, nullptr, std::move(name), parent) {
    setOperand(0, ptr);
    setOperand(1, index);
    if (ptr && ptr->type && ptr->type->isPointer()) {
        type = ptr->type; // Result is also pointer with same address space and pointee
    } else {
        type = IRType::getPointer(IRType::getInt32());
    }
}

AddressSpace GetElementPtrInst::getAddressSpace() const {
    Value* ptr = getPointer();
    if (ptr && ptr->type && ptr->type->isPointer()) {
        return static_cast<PointerIRType*>(ptr->type.get())->addressSpace;
    }
    return AddressSpace::Generic;
}

BinaryInst::BinaryInst(Opcode op, Value* lhs, Value* rhs, std::string name, BasicBlock* parent)
    : Instruction(op, lhs ? lhs->type : IRType::getInt32(), std::move(name), parent) {
    setOperand(0, lhs);
    setOperand(1, rhs);
}

ICmpInst::ICmpInst(CmpPredicate pred, Value* lhs, Value* rhs, std::string name, BasicBlock* parent)
    : Instruction(Opcode::ICmp, IRType::getBool(), std::move(name), parent), predicate(pred) {
    setOperand(0, lhs);
    setOperand(1, rhs);
}

FCmpInst::FCmpInst(CmpPredicate pred, Value* lhs, Value* rhs, std::string name, BasicBlock* parent)
    : Instruction(Opcode::FCmp, IRType::getBool(), std::move(name), parent), predicate(pred) {
    setOperand(0, lhs);
    setOperand(1, rhs);
}

PhiInst::PhiInst(std::shared_ptr<IRType> t, std::string name, BasicBlock* parent)
    : Instruction(Opcode::Phi, std::move(t), std::move(name), parent) {}

void PhiInst::addIncoming(Value* val, BasicBlock* bb) {
    incomingBlocks.push_back(bb);
    size_t idx = operands.size();
    setOperand(idx, val);
}

Value* PhiInst::getIncomingValueForBlock(BasicBlock* bb) const {
    for (size_t i = 0; i < incomingBlocks.size(); ++i) {
        if (incomingBlocks[i] == bb) {
            return operands[i];
        }
    }
    return nullptr;
}

void PhiInst::setIncomingValue(size_t idx, Value* val) {
    setOperand(idx, val);
}

void PhiInst::removeIncomingBlock(BasicBlock* bb) {
    for (size_t i = 0; i < incomingBlocks.size(); ++i) {
        if (incomingBlocks[i] == bb) {
            if (operands[i]) operands[i]->removeUse(this, i);
            incomingBlocks.erase(incomingBlocks.begin() + i);
            operands.erase(operands.begin() + i);
            // Update remaining use indices
            for (size_t j = i; j < operands.size(); ++j) {
                if (operands[j]) {
                    operands[j]->removeUse(this, j + 1);
                    operands[j]->addUse(this, j);
                }
            }
            break;
        }
    }
}

CastInst::CastInst(Opcode op, Value* src, std::shared_ptr<IRType> destType, std::string name, BasicBlock* parent)
    : Instruction(op, std::move(destType), std::move(name), parent) {
    setOperand(0, src);
}

BranchInst::BranchInst(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB, BasicBlock* parent)
    : Instruction(Opcode::Branch, IRType::getVoid(), "", parent),
      trueTarget(trueBB), falseTarget(falseBB) {
    setOperand(0, cond);
}

JumpInst::JumpInst(BasicBlock* tgt, BasicBlock* parent)
    : Instruction(Opcode::Jump, IRType::getVoid(), "", parent), target(tgt) {}

ReturnInst::ReturnInst(Value* val, BasicBlock* parent)
    : Instruction(Opcode::Return, IRType::getVoid(), "", parent) {
    if (val) {
        setOperand(0, val);
    }
}

CallInst::CallInst(std::string funcName, std::shared_ptr<IRType> retType,
                   std::vector<Value*> args, std::string name, BasicBlock* parent)
    : Instruction(Opcode::Call, std::move(retType), std::move(name), parent),
      callee(std::move(funcName)) {
    for (size_t i = 0; i < args.size(); ++i) {
        setOperand(i, args[i]);
    }
}

BasicBlock::BasicBlock(std::string name, Function* p)
    : Value(ValueKind::BasicBlock, IRType::getVoid(), std::move(name)), parent(p) {}

Function::Function(std::string name, bool kernel, std::shared_ptr<IRType> retType, Module* m)
    : Value(ValueKind::Function, retType, std::move(name)),
      parentModule(m), isKernel(kernel), returnType(std::move(retType)) {}

} // namespace kernelforge::ir
