#pragma once

#include "common/Types.h"
#include "common/SourceLoc.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace kernelforge {

struct VarSymbol {
    std::string name;
    std::shared_ptr<Type> type;
    AddressSpace addressSpace = AddressSpace::Local;
    SourceLoc loc;
    bool isParam = false;
    bool isGlobal = false;
};

struct FunctionSymbol {
    std::string name;
    bool isKernel = false;
    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<Type>> paramTypes;
    SourceLoc loc;
};

class Scope {
public:
    explicit Scope(std::shared_ptr<Scope> parent = nullptr) : parent_(std::move(parent)) {}

    std::shared_ptr<Scope> parent() const { return parent_; }

    bool insertVar(VarSymbol sym) {
        if (vars_.find(sym.name) != vars_.end()) {
            return false;
        }
        vars_[sym.name] = std::move(sym);
        return true;
    }

    const VarSymbol* lookupVar(const std::string& name) const {
        auto it = vars_.find(name);
        if (it != vars_.end()) return &it->second;
        if (parent_) return parent_->lookupVar(name);
        return nullptr;
    }

    bool isDeclaredInCurrentScope(const std::string& name) const {
        return vars_.find(name) != vars_.end();
    }

private:
    std::shared_ptr<Scope> parent_;
    std::unordered_map<std::string, VarSymbol> vars_;
};

class SymbolTable {
public:
    SymbolTable() {
        currentScope_ = std::make_shared<Scope>();
    }

    void enterScope() {
        currentScope_ = std::make_shared<Scope>(currentScope_);
    }

    void exitScope() {
        if (currentScope_->parent()) {
            currentScope_ = currentScope_->parent();
        }
    }

    bool insertVar(VarSymbol sym) {
        return currentScope_->insertVar(std::move(sym));
    }

    const VarSymbol* lookupVar(const std::string& name) const {
        return currentScope_->lookupVar(name);
    }

    bool isDeclaredInCurrentScope(const std::string& name) const {
        return currentScope_->isDeclaredInCurrentScope(name);
    }

    bool insertFunction(FunctionSymbol fn) {
        if (functions_.find(fn.name) != functions_.end()) {
            return false;
        }
        functions_[fn.name] = std::move(fn);
        return true;
    }

    const FunctionSymbol* lookupFunction(const std::string& name) const {
        auto it = functions_.find(name);
        if (it != functions_.end()) return &it->second;
        return nullptr;
    }

    std::shared_ptr<Scope> currentScope() const { return currentScope_; }

private:
    std::shared_ptr<Scope> currentScope_;
    std::unordered_map<std::string, FunctionSymbol> functions_;
};

} // namespace kernelforge
