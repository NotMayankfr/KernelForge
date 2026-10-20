#pragma once

#include "Function.h"
#include <string>
#include <vector>
#include <memory>

namespace kernelforge::ir {

class Module {
public:
    std::string name;
    std::vector<std::unique_ptr<GlobalVariable>> globals;
    std::vector<std::unique_ptr<Function>> functions;

    explicit Module(std::string n) : name(std::move(n)) {}
    ~Module() {
        dropAllReferences();
    }

    void dropAllReferences() {
        for (auto& f : functions) {
            if (f) {
                f->dropAllReferences();
            }
        }
    }

    Function* createFunction(std::string fnName, bool isKernel, std::shared_ptr<IRType> retType) {
        auto fn = std::make_unique<Function>(std::move(fnName), isKernel, std::move(retType), this);
        Function* ptr = fn.get();
        functions.push_back(std::move(fn));
        return ptr;
    }

    Function* getFunction(const std::string& fnName) const {
        for (const auto& f : functions) {
            if (f->name == fnName) return f.get();
        }
        return nullptr;
    }

    GlobalVariable* createGlobal(std::shared_ptr<IRType> type, std::string gName, AddressSpace space, size_t sz = 0) {
        auto g = std::make_unique<GlobalVariable>(std::move(type), std::move(gName), space, sz);
        GlobalVariable* ptr = g.get();
        globals.push_back(std::move(g));
        return ptr;
    }

    GlobalVariable* getGlobal(const std::string& gName) const {
        for (const auto& g : globals) {
            if (g->name == gName) return g.get();
        }
        return nullptr;
    }
};

} // namespace kernelforge::ir
