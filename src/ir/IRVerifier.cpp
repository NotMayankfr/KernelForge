#include "IRVerifier.h"
#include <sstream>
#include <iostream>

namespace kernelforge::ir {

bool IRVerifier::verify(Module* mod, std::string* errorMsg) {
    if (!mod) return true;
    for (const auto& func : mod->functions) {
        if (!verify(func.get(), errorMsg)) {
            return false;
        }
    }
    return true;
}

bool IRVerifier::verify(Function* func, std::string* errorMsg) {
    if (!func) return true;
    IRVerifier v;
    v.func_ = func;
    v.domTree_.compute(func);

    bool ok = true;
    ok &= v.checkCFG();
    ok &= v.checkPhis();
    ok &= v.checkDominance();
    ok &= v.checkTypes();

    if (!ok && errorMsg) {
        std::ostringstream ss;
        ss << "IR Verification failed for function '" << func->name << "':\n";
        for (const auto& err : v.errors_) {
            ss << "  - " << err << "\n";
        }
        *errorMsg = ss.str();
    }
    return ok;
}

void IRVerifier::reportError(const std::string& msg) {
    errors_.push_back(msg);
}

bool IRVerifier::checkCFG() {
    bool ok = true;

    for (const auto& bbPtr : func_->basicBlocks) {
        BasicBlock* bb = bbPtr.get();

        if (bb->instructions.empty()) {
            reportError("Basic block '" + bb->name + "' is empty and has no terminator");
            ok = false;
            continue;
        }

        Instruction* term = bb->getTerminator();
        if (!term) {
            reportError("Basic block '" + bb->name + "' does not end with a terminator instruction");
            ok = false;
        }

        // Check that only the last instruction is a terminator
        for (size_t i = 0; i + 1 < bb->instructions.size(); ++i) {
            if (bb->instructions[i]->isTerminator()) {
                reportError("Basic block '" + bb->name + "' has terminator at index " +
                            std::to_string(i) + " before end of block");
                ok = false;
            }
        }

        // Verify predecessor / successor consistency
        for (BasicBlock* succ : bb->successors) {
            auto it = std::find(succ->predecessors.begin(), succ->predecessors.end(), bb);
            if (it == succ->predecessors.end()) {
                reportError("CFG edge from '" + bb->name + "' to '" + succ->name +
                            "' missing in predecessor list of '" + succ->name + "'");
                ok = false;
            }
        }

        for (BasicBlock* pred : bb->predecessors) {
            auto it = std::find(pred->successors.begin(), pred->successors.end(), bb);
            if (it == pred->successors.end()) {
                reportError("CFG edge to '" + bb->name + "' from '" + pred->name +
                            "' missing in successor list of '" + pred->name + "'");
                ok = false;
            }
        }
    }

    return ok;
}

bool IRVerifier::checkPhis() {
    bool ok = true;

    for (const auto& bbPtr : func_->basicBlocks) {
        BasicBlock* bb = bbPtr.get();
        bool seenNonPhi = false;

        for (const auto& instPtr : bb->instructions) {
            Instruction* inst = instPtr.get();
            if (auto* phi = dynamic_cast<PhiInst*>(inst)) {
                if (seenNonPhi) {
                    reportError("Phi node '" + phi->name + "' in block '" + bb->name +
                                "' appears after non-phi instruction");
                    ok = false;
                }

                // Check incoming blocks match CFG predecessors
                for (BasicBlock* pred : bb->predecessors) {
                    bool found = false;
                    for (size_t i = 0; i < phi->numIncoming(); ++i) {
                        if (phi->getIncomingBlock(i) == pred) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        reportError("Phi node '" + phi->name + "' missing incoming entry for predecessor '" +
                                    pred->name + "' in block '" + bb->name + "'");
                        ok = false;
                    }
                }
            } else {
                seenNonPhi = true;
            }
        }
    }

    return ok;
}

bool IRVerifier::checkDominance() {
    bool ok = true;

    for (const auto& bbPtr : func_->basicBlocks) {
        BasicBlock* bb = bbPtr.get();

        for (const auto& instPtr : bb->instructions) {
            Instruction* userInst = instPtr.get();

            if (auto* phi = dynamic_cast<PhiInst*>(userInst)) {
                // Phi uses must be dominated by their defs along the incoming edge
                for (size_t i = 0; i < phi->numIncoming(); ++i) {
                    Value* val = phi->getIncomingValue(i);
                    BasicBlock* incomingBB = phi->getIncomingBlock(i);

                    if (auto* defInst = dynamic_cast<Instruction*>(val)) {
                        if (!domTree_.dominates(defInst->parent, incomingBB)) {
                            reportError("Definition '" + defInst->name + "' in block '" +
                                        defInst->parent->name + "' does not dominate use at incoming block '" +
                                        incomingBB->name + "' of phi '" + phi->name + "'");
                            ok = false;
                        }
                    }
                }
            } else {
                // Regular instruction: def must strictly dominate use
                for (size_t i = 0; i < userInst->numOperands(); ++i) {
                    Value* val = userInst->getOperand(i);
                    if (auto* defInst = dynamic_cast<Instruction*>(val)) {
                        if (!domTree_.dominates(defInst, userInst)) {
                            reportError("Definition of '" + defInst->name + "' in block '" +
                                        defInst->parent->name + "' does not dominate use in '" +
                                        userInst->name + "' in block '" + bb->name + "'");
                            ok = false;
                        }
                    }
                }
            }
        }
    }

    return ok;
}

bool IRVerifier::checkTypes() {
    bool ok = true;

    for (const auto& bbPtr : func_->basicBlocks) {
        BasicBlock* bb = bbPtr.get();
        for (const auto& instPtr : bb->instructions) {
            Instruction* inst = instPtr.get();

            if (inst->isBinary()) {
                Value* lhs = inst->getOperand(0);
                Value* rhs = inst->getOperand(1);
                if (lhs && rhs && !lhs->type->equals(rhs->type.get())) {
                    reportError("Type mismatch in binary instruction '" + inst->name +
                                "': operand 0 is '" + lhs->type->toString() +
                                "', operand 1 is '" + rhs->type->toString() + "'");
                    ok = false;
                }
            } else if (auto* br = dynamic_cast<BranchInst*>(inst)) {
                Value* cond = br->getCondition();
                if (!cond || (!cond->type->isBool() && !cond->type->isInt32())) {
                    reportError("Branch condition in block '" + bb->name + "' must be i1 or i32");
                    ok = false;
                }
            }
        }
    }

    return ok;
}

} // namespace kernelforge::ir
