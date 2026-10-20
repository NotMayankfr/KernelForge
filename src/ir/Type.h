#pragma once

#include "common/Types.h"
#include <string>
#include <memory>

namespace kernelforge::ir {

enum class IRTypeKind {
    Void,
    Int32,
    Float32,
    Bool,
    Pointer,
    Array
};

class IRType {
public:
    IRTypeKind kind;
    explicit IRType(IRTypeKind k) : kind(k) {}
    virtual ~IRType() = default;

    bool isVoid() const { return kind == IRTypeKind::Void; }
    bool isInt32() const { return kind == IRTypeKind::Int32; }
    bool isFloat32() const { return kind == IRTypeKind::Float32; }
    bool isBool() const { return kind == IRTypeKind::Bool; }
    bool isPointer() const { return kind == IRTypeKind::Pointer; }
    bool isArray() const { return kind == IRTypeKind::Array; }
    bool isNumeric() const { return isInt32() || isFloat32(); }

    virtual std::string toString() const = 0;
    virtual bool equals(const IRType* other) const = 0;

    static std::shared_ptr<IRType> getVoid();
    static std::shared_ptr<IRType> getInt32();
    static std::shared_ptr<IRType> getFloat32();
    static std::shared_ptr<IRType> getBool();
    static std::shared_ptr<IRType> getPointer(std::shared_ptr<IRType> pointee, AddressSpace space = AddressSpace::Generic);
    static std::shared_ptr<IRType> getArray(std::shared_ptr<IRType> elem, size_t size, AddressSpace space = AddressSpace::Local);
    static std::shared_ptr<IRType> fromFrontendType(const Type* t);
};

class VoidIRType : public IRType {
public:
    VoidIRType() : IRType(IRTypeKind::Void) {}
    std::string toString() const override { return "void"; }
    bool equals(const IRType* o) const override { return o && o->isVoid(); }
};

class Int32IRType : public IRType {
public:
    Int32IRType() : IRType(IRTypeKind::Int32) {}
    std::string toString() const override { return "i32"; }
    bool equals(const IRType* o) const override { return o && o->isInt32(); }
};

class Float32IRType : public IRType {
public:
    Float32IRType() : IRType(IRTypeKind::Float32) {}
    std::string toString() const override { return "f32"; }
    bool equals(const IRType* o) const override { return o && o->isFloat32(); }
};

class BoolIRType : public IRType {
public:
    BoolIRType() : IRType(IRTypeKind::Bool) {}
    std::string toString() const override { return "i1"; }
    bool equals(const IRType* o) const override { return o && o->isBool(); }
};

class PointerIRType : public IRType {
public:
    std::shared_ptr<IRType> pointee;
    AddressSpace addressSpace;

    PointerIRType(std::shared_ptr<IRType> p, AddressSpace space)
        : IRType(IRTypeKind::Pointer), pointee(std::move(p)), addressSpace(space) {}

    std::string toString() const override {
        std::string s = (addressSpace != AddressSpace::Generic) ? addressSpaceToString(addressSpace) + " " : "";
        s += pointee->toString() + "*";
        return s;
    }

    bool equals(const IRType* o) const override {
        if (!o || !o->isPointer()) return false;
        auto* pt = static_cast<const PointerIRType*>(o);
        return addressSpace == pt->addressSpace && pointee->equals(pt->pointee.get());
    }
};

class ArrayIRType : public IRType {
public:
    std::shared_ptr<IRType> elementType;
    size_t size;
    AddressSpace addressSpace;

    ArrayIRType(std::shared_ptr<IRType> elem, size_t sz, AddressSpace space)
        : IRType(IRTypeKind::Array), elementType(std::move(elem)), size(sz), addressSpace(space) {}

    std::string toString() const override {
        std::string s = (addressSpace != AddressSpace::Generic) ? addressSpaceToString(addressSpace) + " " : "";
        s += "[" + std::to_string(size) + " x " + elementType->toString() + "]";
        return s;
    }

    bool equals(const IRType* o) const override {
        if (!o || !o->isArray()) return false;
        auto* at = static_cast<const ArrayIRType*>(o);
        return addressSpace == at->addressSpace && size == at->size && elementType->equals(at->elementType.get());
    }
};

inline std::shared_ptr<IRType> IRType::getVoid() {
    static auto inst = std::make_shared<VoidIRType>();
    return inst;
}
inline std::shared_ptr<IRType> IRType::getInt32() {
    static auto inst = std::make_shared<Int32IRType>();
    return inst;
}
inline std::shared_ptr<IRType> IRType::getFloat32() {
    static auto inst = std::make_shared<Float32IRType>();
    return inst;
}
inline std::shared_ptr<IRType> IRType::getBool() {
    static auto inst = std::make_shared<BoolIRType>();
    return inst;
}
inline std::shared_ptr<IRType> IRType::getPointer(std::shared_ptr<IRType> pointee, AddressSpace space) {
    return std::make_shared<PointerIRType>(std::move(pointee), space);
}
inline std::shared_ptr<IRType> IRType::getArray(std::shared_ptr<IRType> elem, size_t size, AddressSpace space) {
    return std::make_shared<ArrayIRType>(std::move(elem), size, space);
}

inline std::shared_ptr<IRType> IRType::fromFrontendType(const Type* t) {
    if (!t || t->isVoid()) return getVoid();
    if (t->isInt()) return getInt32();
    if (t->isFloat()) return getFloat32();
    if (t->isBool()) return getBool();
    if (t->isPointer()) {
        auto* pt = static_cast<const PointerType*>(t);
        return getPointer(fromFrontendType(pt->pointeeType.get()), pt->addressSpace);
    }
    if (t->isArray()) {
        auto* at = static_cast<const ArrayType*>(t);
        return getArray(fromFrontendType(at->elementType.get()), at->elementCount, at->addressSpace);
    }
    return getVoid();
}

} // namespace kernelforge::ir
