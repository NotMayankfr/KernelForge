#pragma once

#include "Value.h"
#include <string>
#include <vector>

namespace kernelforge::ir {

class BasicBlock;
class Function;

enum class Opcode {
    // Memory
    Alloca,
    Load,
    Store,
    GetElementPtr,

    // Integer Binary
    Add, Sub, Mul, SDiv, SRem,
    Shl, AShr,
    And, Or, Xor,

    // Floating-Point Binary
    FAdd, FSub, FMul, FDiv,

    // Comparisons
    ICmp,
    FCmp,

    // SSA & Casts
    Phi,
    Select,
    SIToFP,
    FPToSI,
    BitCast,

    // GPU Builtin & Sync
    GetSpecial,
    Sync,

    // Control Flow / Terminators
    Branch,
    Jump,
    Return,
    Call
};

const char* opcodeToString(Opcode op);

enum class CmpPredicate {
    EQ, NE,
    SLT, SLE, SGT, SGE,
    OLT, OLE, OGT, OGE
};

const char* predicateToString(CmpPredicate pred);

class Instruction : public Value {
public:
    Opcode opcode;
    BasicBlock* parent = nullptr;
    std::vector<Value*> operands;

    Instruction(Opcode op, std::shared_ptr<IRType> t, std::string name = "", BasicBlock* p = nullptr)
        : Value(ValueKind::Instruction, std::move(t), std::move(name)), opcode(op), parent(p) {}

    virtual ~Instruction();
    void dropAllReferences();

    bool isTerminator() const {
        return opcode == Opcode::Branch || opcode == Opcode::Jump || opcode == Opcode::Return;
    }

    bool isBinary() const {
        return (opcode >= Opcode::Add && opcode <= Opcode::Xor) ||
               (opcode >= Opcode::FAdd && opcode <= Opcode::FDiv);
    }

    bool isCommutative() const {
        switch (opcode) {
            case Opcode::Add:
            case Opcode::Mul:
            case Opcode::And:
            case Opcode::Or:
            case Opcode::Xor:
            case Opcode::FAdd:
            case Opcode::FMul:
                return true;
            default:
                return false;
        }
    }

    bool hasSideEffects() const {
        return opcode == Opcode::Store || opcode == Opcode::Sync ||
               opcode == Opcode::Call || isTerminator();
    }

    void setOperand(size_t idx, Value* newVal);
    Value* getOperand(size_t idx) const { return operands[idx]; }
    size_t numOperands() const { return operands.size(); }
};

// Memory instructions
class AllocaInst : public Instruction {
public:
    std::shared_ptr<IRType> allocatedType;
    AddressSpace addressSpace;
    size_t arraySize = 0;

    AllocaInst(std::shared_ptr<IRType> allocType, AddressSpace space = AddressSpace::Local,
               size_t arrSz = 0, std::string name = "", BasicBlock* parent = nullptr)
        : Instruction(Opcode::Alloca, IRType::getPointer(allocType, space), std::move(name), parent),
          allocatedType(std::move(allocType)), addressSpace(space), arraySize(arrSz) {}
};

class LoadInst : public Instruction {
public:
    LoadInst(Value* ptr, std::string name = "", BasicBlock* parent = nullptr);
    Value* getPointer() const { return operands[0]; }
    AddressSpace getAddressSpace() const;
};

class StoreInst : public Instruction {
public:
    StoreInst(Value* val, Value* ptr, BasicBlock* parent = nullptr);
    Value* getValue() const { return operands[0]; }
    Value* getPointer() const { return operands[1]; }
    AddressSpace getAddressSpace() const;
};

class GetElementPtrInst : public Instruction {
public:
    GetElementPtrInst(Value* ptr, Value* index, std::string name = "", BasicBlock* parent = nullptr);
    Value* getPointer() const { return operands[0]; }
    Value* getIndex() const { return operands[1]; }
    AddressSpace getAddressSpace() const;
};

// Binary operations
class BinaryInst : public Instruction {
public:
    BinaryInst(Opcode op, Value* lhs, Value* rhs, std::string name = "", BasicBlock* parent = nullptr);
    Value* getLHS() const { return operands[0]; }
    Value* getRHS() const { return operands[1]; }
};

class ICmpInst : public Instruction {
public:
    CmpPredicate predicate;
    ICmpInst(CmpPredicate pred, Value* lhs, Value* rhs, std::string name = "", BasicBlock* parent = nullptr);
    Value* getLHS() const { return operands[0]; }
    Value* getRHS() const { return operands[1]; }
};

class FCmpInst : public Instruction {
public:
    CmpPredicate predicate;
    FCmpInst(CmpPredicate pred, Value* lhs, Value* rhs, std::string name = "", BasicBlock* parent = nullptr);
    Value* getLHS() const { return operands[0]; }
    Value* getRHS() const { return operands[1]; }
};

// Phi node
class PhiInst : public Instruction {
public:
    PhiInst(std::shared_ptr<IRType> t, std::string name = "", BasicBlock* parent = nullptr);

    void addIncoming(Value* val, BasicBlock* bb);
    size_t numIncoming() const { return incomingBlocks.size(); }
    Value* getIncomingValue(size_t idx) const { return operands[idx]; }
    BasicBlock* getIncomingBlock(size_t idx) const { return incomingBlocks[idx]; }
    Value* getIncomingValueForBlock(BasicBlock* bb) const;
    void setIncomingValue(size_t idx, Value* val);
    void removeIncomingBlock(BasicBlock* bb);

    std::vector<BasicBlock*> incomingBlocks;
};

// Casts
class CastInst : public Instruction {
public:
    CastInst(Opcode op, Value* src, std::shared_ptr<IRType> destType,
             std::string name = "", BasicBlock* parent = nullptr);
    Value* getSource() const { return operands[0]; }
};

// Builtin & GPU Sync
class GetSpecialInst : public Instruction {
public:
    std::string specialReg; // "threadIdx.x", "blockIdx.x", "blockDim.x", "gridDim.x"
    GetSpecialInst(std::string reg, std::string name = "", BasicBlock* parent = nullptr)
        : Instruction(Opcode::GetSpecial, IRType::getInt32(), std::move(name), parent),
          specialReg(std::move(reg)) {}
};

class SyncInst : public Instruction {
public:
    SyncInst(BasicBlock* parent = nullptr)
        : Instruction(Opcode::Sync, IRType::getVoid(), "", parent) {}
};

// Control Flow / Terminators
class BranchInst : public Instruction {
public:
    BranchInst(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB, BasicBlock* parent = nullptr);
    Value* getCondition() const { return operands[0]; }
    BasicBlock* getTrueTarget() const { return trueTarget; }
    BasicBlock* getFalseTarget() const { return falseTarget; }
    void setTrueTarget(BasicBlock* bb) { trueTarget = bb; }
    void setFalseTarget(BasicBlock* bb) { falseTarget = bb; }

    BasicBlock* trueTarget;
    BasicBlock* falseTarget;
};

class JumpInst : public Instruction {
public:
    JumpInst(BasicBlock* target, BasicBlock* parent = nullptr);
    BasicBlock* getTarget() const { return target; }
    void setTarget(BasicBlock* bb) { target = bb; }

    BasicBlock* target;
};

class ReturnInst : public Instruction {
public:
    ReturnInst(Value* val = nullptr, BasicBlock* parent = nullptr);
    Value* getReturnValue() const { return operands.empty() ? nullptr : operands[0]; }
};

class CallInst : public Instruction {
public:
    std::string callee;
    CallInst(std::string funcName, std::shared_ptr<IRType> retType,
             std::vector<Value*> args, std::string name = "", BasicBlock* parent = nullptr);
    size_t numArgs() const { return operands.size(); }
    Value* getArg(size_t idx) const { return operands[idx]; }
};

} // namespace kernelforge::ir
