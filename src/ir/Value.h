#pragma once

#include "Type.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace kernelforge::ir {

class Instruction;

struct Use {
    Instruction* user;
    size_t operandIndex;
};

enum class ValueKind {
    ConstantInt,
    ConstantFloat,
    ConstantBool,
    Argument,
    Instruction,
    BasicBlock,
    Function,
    GlobalVariable
};

class Value {
public:
    ValueKind kind;
    std::shared_ptr<IRType> type;
    std::string name;
    std::vector<Use> uses;

    Value(ValueKind k, std::shared_ptr<IRType> t, std::string n = "")
        : kind(k), type(std::move(t)), name(std::move(n)) {}
    virtual ~Value() = default;

    bool isConstantInt() const { return kind == ValueKind::ConstantInt; }
    bool isConstantFloat() const { return kind == ValueKind::ConstantFloat; }
    bool isConstantBool() const { return kind == ValueKind::ConstantBool; }
    bool isConstant() const { return isConstantInt() || isConstantFloat() || isConstantBool(); }
    bool isArgument() const { return kind == ValueKind::Argument; }
    bool isInstruction() const { return kind == ValueKind::Instruction; }
    bool isBasicBlock() const { return kind == ValueKind::BasicBlock; }
    bool isFunction() const { return kind == ValueKind::Function; }

    void addUse(Instruction* user, size_t operandIndex) {
        uses.push_back({user, operandIndex});
    }

    void removeUse(Instruction* user, size_t operandIndex) {
        uses.erase(std::remove_if(uses.begin(), uses.end(),
            [user, operandIndex](const Use& u) {
                return u.user == user && u.operandIndex == operandIndex;
            }), uses.end());
    }

    bool hasUses() const { return !uses.empty(); }
    size_t useCount() const { return uses.size(); }

    void replaceAllUsesWith(Value* newVal);
};

class ConstantInt : public Value {
public:
    int32_t value;
    ConstantInt(int32_t v)
        : Value(ValueKind::ConstantInt, IRType::getInt32(), std::to_string(v)), value(v) {}
};

class ConstantFloat : public Value {
public:
    float value;
    ConstantFloat(float v)
        : Value(ValueKind::ConstantFloat, IRType::getFloat32(), std::to_string(v) + "f"), value(v) {}
};

class ConstantBool : public Value {
public:
    bool value;
    ConstantBool(bool v)
        : Value(ValueKind::ConstantBool, IRType::getBool(), v ? "true" : "false"), value(v) {}
};

class Argument : public Value {
public:
    size_t index;
    Argument(std::shared_ptr<IRType> t, std::string n, size_t idx)
        : Value(ValueKind::Argument, std::move(t), std::move(n)), index(idx) {}
};

class GlobalVariable : public Value {
public:
    AddressSpace addressSpace;
    size_t arraySize = 0;

    GlobalVariable(std::shared_ptr<IRType> t, std::string n, AddressSpace space, size_t sz = 0)
        : Value(ValueKind::GlobalVariable, std::move(t), std::move(n)),
          addressSpace(space), arraySize(sz) {}
};

} // namespace kernelforge::ir
