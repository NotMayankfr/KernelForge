#include "PassManager.h"
#include "ConstantFolding.h"
#include "AlgebraicSimplification.h"
#include "DeadCodeElimination.h"
#include "CommonSubexpressionElimination.h"
#include "StrengthReduction.h"
#include "LoopInvariantCodeMotion.h"
#include "ir/IRVerifier.h"
#include <iostream>
#include <stdexcept>

namespace kernelforge::opt {

PassManager::PassManager(bool verifyAfterPass) : verifyAfterPass_(verifyAfterPass) {}

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passNames_.push_back(pass->getName());
    passes_.push_back(std::move(pass));
}

bool PassManager::run(ir::Function* func) {
    bool changedAny = false;

    for (const auto& pass : passes_) {
        bool changed = pass->runOnFunction(func);
        if (changed) {
            changedAny = true;
            if (verifyAfterPass_) {
                std::string err;
                if (!ir::IRVerifier::verify(func, &err)) {
                    std::cerr << "Optimizer verification failed after pass: " << pass->getName() << "\n";
                    std::cerr << err << "\n";
                    throw std::runtime_error("Optimizer corrupted IR after " + std::string(pass->getName()));
                }
            }
        }
    }

    return changedAny;
}

bool PassManager::run(ir::Module* mod) {
    bool changed = false;
    for (const auto& f : mod->functions) {
        if (run(f.get())) {
            changed = true;
        }
    }
    return changed;
}

std::unique_ptr<PassManager> PassManager::createO0() {
    return std::make_unique<PassManager>(true);
}

std::unique_ptr<PassManager> PassManager::createO1() {
    auto pm = std::make_unique<PassManager>(true);
    pm->addPass(std::make_unique<ConstantFoldingPass>());
    pm->addPass(std::make_unique<AlgebraicSimplificationPass>());
    pm->addPass(std::make_unique<DeadCodeEliminationPass>());
    return pm;
}

std::unique_ptr<PassManager> PassManager::createO2() {
    auto pm = std::make_unique<PassManager>(true);
    pm->addPass(std::make_unique<ConstantFoldingPass>());
    pm->addPass(std::make_unique<AlgebraicSimplificationPass>());
    pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
    pm->addPass(std::make_unique<StrengthReductionPass>());
    pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>());
    pm->addPass(std::make_unique<ConstantFoldingPass>());
    pm->addPass(std::make_unique<AlgebraicSimplificationPass>());
    pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
    pm->addPass(std::make_unique<DeadCodeEliminationPass>());
    return pm;
}

} // namespace kernelforge::opt
