#include "IRPrinter.h"
#include <sstream>

namespace kernelforge::ir {

std::string IRPrinter::print(const Value* val) {
    if (!val) return "null";
    if (auto* ci = dynamic_cast<const ConstantInt*>(val)) {
        return std::to_string(ci->value);
    }
    if (auto* cf = dynamic_cast<const ConstantFloat*>(val)) {
        return std::to_string(cf->value) + "f";
    }
    if (auto* cb = dynamic_cast<const ConstantBool*>(val)) {
        return cb->value ? "true" : "false";
    }
    if (auto* bb = dynamic_cast<const BasicBlock*>(val)) {
        return "%" + bb->name;
    }
    return "%" + val->name;
}

std::string IRPrinter::print(const Instruction* inst) {
    if (!inst) return "";
    std::ostringstream ss;

    if (!inst->type->isVoid() && inst->opcode != Opcode::Branch && inst->opcode != Opcode::Jump) {
        ss << "%" << inst->name << " = ";
    }

    switch (inst->opcode) {
        case Opcode::Alloca: {
            auto* alloca = static_cast<const AllocaInst*>(inst);
            ss << "alloca " << addressSpaceToString(alloca->addressSpace) << " "
               << alloca->allocatedType->toString();
            if (alloca->arraySize > 0) {
                ss << "[" << alloca->arraySize << "]";
            }
            break;
        }
        case Opcode::Load: {
            auto* ld = static_cast<const LoadInst*>(inst);
            ss << "load " << ld->type->toString() << ", "
               << ld->getPointer()->type->toString() << " " << print(ld->getPointer());
            break;
        }
        case Opcode::Store: {
            auto* st = static_cast<const StoreInst*>(inst);
            ss << "store " << st->getValue()->type->toString() << " " << print(st->getValue())
               << ", " << st->getPointer()->type->toString() << " " << print(st->getPointer());
            break;
        }
        case Opcode::GetElementPtr: {
            auto* gep = static_cast<const GetElementPtrInst*>(inst);
            ss << "gep " << gep->getPointer()->type->toString() << " " << print(gep->getPointer())
               << ", i32 " << print(gep->getIndex());
            break;
        }
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::SRem:
        case Opcode::Shl:
        case Opcode::AShr:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv: {
            auto* bin = static_cast<const BinaryInst*>(inst);
            ss << opcodeToString(bin->opcode) << " " << bin->type->toString() << " "
               << print(bin->getLHS()) << ", " << print(bin->getRHS());
            break;
        }
        case Opcode::ICmp: {
            auto* cmp = static_cast<const ICmpInst*>(inst);
            ss << "icmp " << predicateToString(cmp->predicate) << " "
               << cmp->getLHS()->type->toString() << " " << print(cmp->getLHS())
               << ", " << print(cmp->getRHS());
            break;
        }
        case Opcode::FCmp: {
            auto* cmp = static_cast<const FCmpInst*>(inst);
            ss << "fcmp " << predicateToString(cmp->predicate) << " "
               << cmp->getLHS()->type->toString() << " " << print(cmp->getLHS())
               << ", " << print(cmp->getRHS());
            break;
        }
        case Opcode::Phi: {
            auto* phi = static_cast<const PhiInst*>(inst);
            ss << "phi " << phi->type->toString();
            for (size_t i = 0; i < phi->numIncoming(); ++i) {
                if (i > 0) ss << ",";
                ss << " [ " << print(phi->getIncomingValue(i)) << ", %"
                   << phi->getIncomingBlock(i)->name << " ]";
            }
            break;
        }
        case Opcode::SIToFP:
        case Opcode::FPToSI:
        case Opcode::BitCast: {
            auto* cast = static_cast<const CastInst*>(inst);
            ss << opcodeToString(cast->opcode) << " " << cast->getSource()->type->toString()
               << " " << print(cast->getSource()) << " to " << cast->type->toString();
            break;
        }
        case Opcode::GetSpecial: {
            auto* sp = static_cast<const GetSpecialInst*>(inst);
            ss << "get_special \"" << sp->specialReg << "\"";
            break;
        }
        case Opcode::Sync: {
            ss << "sync";
            break;
        }
        case Opcode::Branch: {
            auto* br = static_cast<const BranchInst*>(inst);
            ss << "br " << br->getCondition()->type->toString() << " " << print(br->getCondition())
               << ", label %" << br->getTrueTarget()->name
               << ", label %" << br->getFalseTarget()->name;
            break;
        }
        case Opcode::Jump: {
            auto* jmp = static_cast<const JumpInst*>(inst);
            ss << "jmp label %" << jmp->getTarget()->name;
            break;
        }
        case Opcode::Return: {
            auto* ret = static_cast<const ReturnInst*>(inst);
            ss << "ret";
            if (ret->getReturnValue()) {
                ss << " " << ret->getReturnValue()->type->toString() << " " << print(ret->getReturnValue());
            } else {
                ss << " void";
            }
            break;
        }
        case Opcode::Call: {
            auto* call = static_cast<const CallInst*>(inst);
            ss << "call " << call->type->toString() << " @" << call->callee << "(";
            for (size_t i = 0; i < call->numArgs(); ++i) {
                if (i > 0) ss << ", ";
                ss << call->getArg(i)->type->toString() << " " << print(call->getArg(i));
            }
            ss << ")";
            break;
        }
        default:
            ss << opcodeToString(inst->opcode);
            break;
    }

    return ss.str();
}

std::string IRPrinter::print(const BasicBlock* bb) {
    if (!bb) return "";
    std::ostringstream ss;
    ss << bb->name << ":";
    if (!bb->predecessors.empty()) {
        ss << "    ; preds: ";
        for (size_t i = 0; i < bb->predecessors.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "%" << bb->predecessors[i]->name;
        }
    }
    ss << "\n";

    for (const auto& inst : bb->instructions) {
        ss << "  " << print(inst.get()) << "\n";
    }
    return ss.str();
}

std::string IRPrinter::print(const Function* func) {
    if (!func) return "";
    std::ostringstream ss;

    if (func->isKernel) ss << "kernel ";
    else ss << "func ";

    ss << func->returnType->toString() << " @" << func->name << "(";
    for (size_t i = 0; i < func->arguments.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << func->arguments[i]->type->toString() << " %" << func->arguments[i]->name;
    }
    ss << ") {\n";

    for (const auto& bb : func->basicBlocks) {
        ss << print(bb.get());
    }

    ss << "}\n";
    return ss.str();
}

std::string IRPrinter::print(const Module* mod) {
    if (!mod) return "";
    std::ostringstream ss;

    for (const auto& g : mod->globals) {
        ss << "global " << addressSpaceToString(g->addressSpace) << " "
           << g->type->toString() << " @" << g->name;
        if (g->arraySize > 0) {
            ss << "[" << g->arraySize << "]";
        }
        ss << "\n";
    }

    if (!mod->globals.empty()) ss << "\n";

    for (const auto& f : mod->functions) {
        ss << print(f.get()) << "\n";
    }

    return ss.str();
}

} // namespace kernelforge::ir
