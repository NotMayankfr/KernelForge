#pragma once

#include <string>
#include <memory>
#include <cassert>

namespace kernelforge {

enum class AddressSpace {
    Generic,
    Global,
    Shared,
    Local,
    Constant
};

inline std::string addressSpaceToString(AddressSpace space) {
    switch (space) {
        case AddressSpace::Generic: return "generic";
        case AddressSpace::Global:  return "global";
        case AddressSpace::Shared:  return "shared";
        case AddressSpace::Local:   return "local";
        case AddressSpace::Constant:return "const";
    }
    return "unknown";
}

enum class TypeKind {
    Void,
    Bool,
    Int,
    Float,
    Pointer,
    Array,
    Function
};

class Type {
public:
    virtual ~Type() = default;
    TypeKind kind;

    explicit Type(TypeKind k) : kind(k) {}

    bool isVoid() const { return kind == TypeKind::Void; }
    bool isBool() const { return kind == TypeKind::Bool; }
    bool isInt() const { return kind == TypeKind::Int; }
    bool isFloat() const { return kind == TypeKind::Float; }
    bool isPointer() const { return kind == TypeKind::Pointer; }
    bool isArray() const { return kind == TypeKind::Array; }
    bool isFunction() const { return kind == TypeKind::Function; }
    bool isNumeric() const { return isInt() || isFloat(); }

    virtual std::string toString() const = 0;
    virtual bool equals(const Type* other) const = 0;
    virtual size_t sizeInBytes() const = 0;
};

class VoidType : public Type {
public:
    VoidType() : Type(TypeKind::Void) {}
    std::string toString() const override { return "void"; }
    bool equals(const Type* other) const override { return other && other->isVoid(); }
    size_t sizeInBytes() const override { return 0; }
    static std::shared_ptr<VoidType> get() {
        static auto instance = std::make_shared<VoidType>();
        return instance;
    }
};

class BoolType : public Type {
public:
    BoolType() : Type(TypeKind::Bool) {}
    std::string toString() const override { return "bool"; }
    bool equals(const Type* other) const override { return other && other->isBool(); }
    size_t sizeInBytes() const override { return 1; }
    static std::shared_ptr<BoolType> get() {
        static auto instance = std::make_shared<BoolType>();
        return instance;
    }
};

class IntType : public Type {
public:
    IntType() : Type(TypeKind::Int) {}
    std::string toString() const override { return "int"; }
    bool equals(const Type* other) const override { return other && other->isInt(); }
    size_t sizeInBytes() const override { return 4; }
    static std::shared_ptr<IntType> get() {
        static auto instance = std::make_shared<IntType>();
        return instance;
    }
};

class FloatType : public Type {
public:
    FloatType() : Type(TypeKind::Float) {}
    std::string toString() const override { return "float"; }
    bool equals(const Type* other) const override { return other && other->isFloat(); }
    size_t sizeInBytes() const override { return 4; }
    static std::shared_ptr<FloatType> get() {
        static auto instance = std::make_shared<FloatType>();
        return instance;
    }
};

class PointerType : public Type {
public:
    std::shared_ptr<Type> pointeeType;
    AddressSpace addressSpace;

    PointerType(std::shared_ptr<Type> pointee, AddressSpace space = AddressSpace::Generic)
        : Type(TypeKind::Pointer), pointeeType(std::move(pointee)), addressSpace(space) {}

    std::string toString() const override {
        std::string s = (addressSpace != AddressSpace::Generic) ? addressSpaceToString(addressSpace) + " " : "";
        s += pointeeType->toString() + "*";
        return s;
    }

    bool equals(const Type* other) const override {
        if (!other || !other->isPointer()) return false;
        auto* pt = static_cast<const PointerType*>(other);
        return addressSpace == pt->addressSpace && pointeeType->equals(pt->pointeeType.get());
    }

    size_t sizeInBytes() const override { return 4; } // 32-bit virtual GPU address
};

class ArrayType : public Type {
public:
    std::shared_ptr<Type> elementType;
    size_t elementCount;
    AddressSpace addressSpace;

    ArrayType(std::shared_ptr<Type> elem, size_t count, AddressSpace space = AddressSpace::Local)
        : Type(TypeKind::Array), elementType(std::move(elem)), elementCount(count), addressSpace(space) {}

    std::string toString() const override {
        std::string s = (addressSpace != AddressSpace::Generic) ? addressSpaceToString(addressSpace) + " " : "";
        s += elementType->toString() + "[" + std::to_string(elementCount) + "]";
        return s;
    }

    bool equals(const Type* other) const override {
        if (!other || !other->isArray()) return false;
        auto* at = static_cast<const ArrayType*>(other);
        return addressSpace == at->addressSpace &&
               elementCount == at->elementCount &&
               elementType->equals(at->elementType.get());
    }

    size_t sizeInBytes() const override {
        return elementType->sizeInBytes() * elementCount;
    }
};

} // namespace kernelforge
