#pragma once

#include "common/TargetInfo.h"
#include "common/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <iostream>

namespace kernelforge::backend {

enum class RegClass {
    Int,        // %r0, %r1, ...
    Float,      // %f0, %f1, ...
    Pred,       // %p0, %p1, ...
    Special     // %tid.x, %ctaid.x, etc.
};

struct Register {
    RegClass regClass;
    int id;                 // virtual or physical ID
    bool isPhysical = false;
    std::string specialName; // for special registers

    bool operator==(const Register& o) const {
        if (regClass != o.regClass) return false;
        if (regClass == RegClass::Special) return specialName == o.specialName;
        return id == o.id && isPhysical == o.isPhysical;
    }

    bool operator!=(const Register& o) const { return !(*this == o); }

    std::string toString() const {
        if (regClass == RegClass::Special) return "%" + specialName;
        std::string prefix;
        switch (regClass) {
            case RegClass::Int:   prefix = isPhysical ? "%R" : "%r"; break;
            case RegClass::Float: prefix = isPhysical ? "%F" : "%f"; break;
            case RegClass::Pred:  prefix = isPhysical ? "%P" : "%p"; break;
            default: prefix = "%v"; break;
        }
        return prefix + std::to_string(id);
    }
};

enum class OperandKind {
    Register,
    ImmediateInt,
    ImmediateFloat,
    ImmediateBool,
    Label,
    Memory // [baseReg + offset]
};

struct MachineOperand {
    OperandKind kind;
    Register reg;
    int32_t immInt = 0;
    float immFloat = 0.0f;
    bool immBool = false;
    std::string label;
    int32_t memOffset = 0;

    static MachineOperand createReg(Register r) {
        MachineOperand op;
        op.kind = OperandKind::Register;
        op.reg = r;
        return op;
    }

    static MachineOperand createImmInt(int32_t val) {
        MachineOperand op;
        op.kind = OperandKind::ImmediateInt;
        op.immInt = val;
        return op;
    }

    static MachineOperand createImmFloat(float val) {
        MachineOperand op;
        op.kind = OperandKind::ImmediateFloat;
        op.immFloat = val;
        return op;
    }

    static MachineOperand createImmBool(bool val) {
        MachineOperand op;
        op.kind = OperandKind::ImmediateBool;
        op.immBool = val;
        return op;
    }

    static MachineOperand createLabel(std::string l) {
        MachineOperand op;
        op.kind = OperandKind::Label;
        op.label = std::move(l);
        return op;
    }

    static MachineOperand createMem(Register base, int32_t offset = 0) {
        MachineOperand op;
        op.kind = OperandKind::Memory;
        op.reg = base;
        op.memOffset = offset;
        return op;
    }

    bool isReg() const { return kind == OperandKind::Register; }
    bool isImm() const { return kind == OperandKind::ImmediateInt || kind == OperandKind::ImmediateFloat || kind == OperandKind::ImmediateBool; }
    bool isLabel() const { return kind == OperandKind::Label; }
    bool isMem() const { return kind == OperandKind::Memory; }

    std::string toString() const {
        switch (kind) {
            case OperandKind::Register: return reg.toString();
            case OperandKind::ImmediateInt: return std::to_string(immInt);
            case OperandKind::ImmediateFloat: return std::to_string(immFloat) + "f";
            case OperandKind::ImmediateBool: return immBool ? "1" : "0";
            case OperandKind::Label: return "$" + label;
            case OperandKind::Memory:
                if (memOffset == 0) return "[" + reg.toString() + "]";
                return "[" + reg.toString() + " + " + std::to_string(memOffset) + "]";
        }
        return "";
    }
};

enum class MachineOpcode {
    // Arithmetic Int
    ADD_S32, SUB_S32, MUL_S32, DIV_S32, REM_S32,
    SHL_B32, SHR_S32, AND_B32, OR_B32, XOR_B32,

    // Arithmetic Float
    ADD_F32, SUB_F32, MUL_F32, DIV_F32, NEG_F32,

    // Compare
    SETP_EQ_S32, SETP_NE_S32, SETP_LT_S32, SETP_LE_S32, SETP_GT_S32, SETP_GE_S32,
    SETP_EQ_F32, SETP_NE_F32, SETP_LT_F32, SETP_LE_F32, SETP_GT_F32, SETP_GE_F32,

    // Move & Convert
    MOV_B32, MOV_F32,
    CVT_F32_S32, CVT_S32_F32,

    // Memory
    LD_GLOBAL_S32, LD_GLOBAL_F32, ST_GLOBAL_S32, ST_GLOBAL_F32,
    LD_SHARED_S32, LD_SHARED_F32, ST_SHARED_S32, ST_SHARED_F32,
    LD_LOCAL_S32,  LD_LOCAL_F32,  ST_LOCAL_S32,  ST_LOCAL_F32,
    LD_PARAM_S32,  LD_PARAM_F32,

    // Control & Synchronization
    BRA,
    CALL,
    RET,
    EXIT,
    BAR_SYNC,

    // Pseudo
    LABEL
};

const char* machineOpcodeToString(MachineOpcode op);

struct MachineInst {
    MachineOpcode opcode;
    std::vector<MachineOperand> operands;
    bool hasPredicate = false;
    Register predReg;
    bool predInvert = false;
    uint32_t id = 0; // Linear sequence number for liveness analysis

    MachineInst(MachineOpcode op) : opcode(op) {}

    void addOperand(MachineOperand op) {
        operands.push_back(std::move(op));
    }

    void setPredicate(Register p, bool invert = false) {
        hasPredicate = true;
        predReg = p;
        predInvert = invert;
    }

    std::string toString() const;
};

struct MachineBasicBlock {
    std::string name;
    std::vector<std::unique_ptr<MachineInst>> instructions;
    std::vector<MachineBasicBlock*> predecessors;
    std::vector<MachineBasicBlock*> successors;

    explicit MachineBasicBlock(std::string n) : name(std::move(n)) {}

    void addInstruction(std::unique_ptr<MachineInst> inst) {
        instructions.push_back(std::move(inst));
    }
};

struct MachineFunction {
    std::string name;
    bool isKernel = false;
    std::vector<std::string> paramNames;
    std::vector<std::unique_ptr<MachineBasicBlock>> blocks;
    uint32_t localStackSize = 0; // in bytes

    explicit MachineFunction(std::string n, bool kernel = false)
        : name(std::move(n)), isKernel(kernel) {}

    MachineBasicBlock* createBlock(std::string name) {
        auto bb = std::make_unique<MachineBasicBlock>(std::move(name));
        MachineBasicBlock* ptr = bb.get();
        blocks.push_back(std::move(bb));
        return ptr;
    }
};

struct MachineModule {
    std::string name;
    std::vector<std::unique_ptr<MachineFunction>> functions;

    explicit MachineModule(std::string n) : name(std::move(n)) {}

    MachineFunction* createFunction(std::string fnName, bool isKernel = false) {
        auto fn = std::make_unique<MachineFunction>(std::move(fnName), isKernel);
        MachineFunction* ptr = fn.get();
        functions.push_back(std::move(fn));
        return ptr;
    }
};

} // namespace kernelforge::backend
