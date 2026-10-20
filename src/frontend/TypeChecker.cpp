#include "TypeChecker.h"

namespace kernelforge {

TypeChecker::TypeChecker(DiagnosticEngine& diags) : diags_(diags) {}

bool TypeChecker::check(ProgramAST* program) {
    if (!program) return false;
    program->accept(this);
    return !diags_.hasErrors();
}

bool TypeChecker::areTypesCompatible(const Type* expected, const Type* actual) {
    if (!expected || !actual) return false;
    if (expected->equals(actual)) return true;

    // Allow implicit promotion from Int to Float
    if (expected->isFloat() && actual->isInt()) return true;

    // Allow bool in int context or int in bool context
    if (expected->isBool() && actual->isInt()) return true;
    if (expected->isInt() && actual->isBool()) return true;

    // Allow pointer compatibility if pointees match and target has generic address space
    if (expected->isPointer() && actual->isPointer()) {
        auto* expP = static_cast<const PointerType*>(expected);
        auto* actP = static_cast<const PointerType*>(actual);
        if (expP->pointeeType->equals(actP->pointeeType.get())) {
            if (expP->addressSpace == AddressSpace::Generic || expP->addressSpace == actP->addressSpace) {
                return true;
            }
        }
    }

    return false;
}

bool TypeChecker::checkLValue(const ExprAST* expr) {
    if (dynamic_cast<const VarExprAST*>(expr)) return true;
    if (dynamic_cast<const IndexExprAST*>(expr)) return true;
    return false;
}

void TypeChecker::visit(IntLiteralExprAST* node) {
    node->inferredType = IntType::get();
}

void TypeChecker::visit(FloatLiteralExprAST* node) {
    node->inferredType = FloatType::get();
}

void TypeChecker::visit(BoolLiteralExprAST* node) {
    node->inferredType = BoolType::get();
}

void TypeChecker::visit(VarExprAST* node) {
    const VarSymbol* sym = symTab_.lookupVar(node->name);
    if (!sym) {
        diags_.error(node->loc, "undeclared identifier '" + node->name + "'");
        node->inferredType = IntType::get(); // fallback
        return;
    }
    node->inferredType = sym->type;
}

void TypeChecker::visit(BuiltinExprAST* node) {
    node->inferredType = IntType::get();
}

void TypeChecker::visit(BinaryExprAST* node) {
    node->lhs->accept(this);
    node->rhs->accept(this);

    auto lType = node->lhs->inferredType;
    auto rType = node->rhs->inferredType;

    if (!lType || !rType) {
        node->inferredType = IntType::get();
        return;
    }

    switch (node->op) {
        case BinaryOp::Assign:
        case BinaryOp::AddAssign:
        case BinaryOp::SubAssign:
        case BinaryOp::MulAssign:
        case BinaryOp::DivAssign: {
            if (!checkLValue(node->lhs.get())) {
                diags_.error(node->loc, "left-hand side of assignment must be an lvalue");
            }
            if (!areTypesCompatible(lType.get(), rType.get())) {
                diags_.error(node->loc, "cannot assign value of type '" + rType->toString() +
                                        "' to variable of type '" + lType->toString() + "'");
            }
            node->inferredType = lType;
            break;
        }
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div: {
            if (!lType->isNumeric() || !rType->isNumeric()) {
                diags_.error(node->loc, "arithmetic operations require numeric operands, got '" +
                                        lType->toString() + "' and '" + rType->toString() + "'");
                node->inferredType = IntType::get();
            } else if (lType->isFloat() || rType->isFloat()) {
                node->inferredType = FloatType::get();
            } else {
                node->inferredType = IntType::get();
            }
            break;
        }
        case BinaryOp::Mod:
        case BinaryOp::Shl:
        case BinaryOp::Shr:
        case BinaryOp::And:
        case BinaryOp::Or:
        case BinaryOp::Xor: {
            if (!lType->isInt() || !rType->isInt()) {
                diags_.error(node->loc, "bitwise/modulo operations require integer operands, got '" +
                                        lType->toString() + "' and '" + rType->toString() + "'");
            }
            node->inferredType = IntType::get();
            break;
        }
        case BinaryOp::LAnd:
        case BinaryOp::LOr: {
            node->inferredType = BoolType::get();
            break;
        }
        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge: {
            if (!areTypesCompatible(lType.get(), rType.get()) &&
                !areTypesCompatible(rType.get(), lType.get())) {
                diags_.error(node->loc, "comparison between incompatible types '" +
                                        lType->toString() + "' and '" + rType->toString() + "'");
            }
            node->inferredType = BoolType::get();
            break;
        }
    }
}

void TypeChecker::visit(UnaryExprAST* node) {
    node->operand->accept(this);
    auto t = node->operand->inferredType;
    if (!t) {
        node->inferredType = IntType::get();
        return;
    }

    switch (node->op) {
        case UnaryOp::Neg:
            if (!t->isNumeric()) {
                diags_.error(node->loc, "unary '-' requires numeric operand, got '" + t->toString() + "'");
            }
            node->inferredType = t;
            break;
        case UnaryOp::Not:
            node->inferredType = BoolType::get();
            break;
        case UnaryOp::BitNot:
            if (!t->isInt()) {
                diags_.error(node->loc, "unary '~' requires integer operand, got '" + t->toString() + "'");
            }
            node->inferredType = IntType::get();
            break;
    }
}

void TypeChecker::visit(IndexExprAST* node) {
    node->base->accept(this);
    node->index->accept(this);

    auto baseT = node->base->inferredType;
    auto idxT = node->index->inferredType;

    if (!idxT || !idxT->isInt()) {
        diags_.error(node->loc, "array index must be an integer, got '" +
                                (idxT ? idxT->toString() : "unknown") + "'");
    }

    if (!baseT) {
        node->inferredType = FloatType::get();
        return;
    }

    if (baseT->isPointer()) {
        auto* pt = static_cast<PointerType*>(baseT.get());
        node->inferredType = pt->pointeeType;
    } else if (baseT->isArray()) {
        auto* at = static_cast<ArrayType*>(baseT.get());
        node->inferredType = at->elementType;
    } else {
        diags_.error(node->loc, "subscripted value is neither an array nor a pointer, got '" +
                                baseT->toString() + "'");
        node->inferredType = FloatType::get();
    }
}

void TypeChecker::visit(CallExprAST* node) {
    const FunctionSymbol* fn = symTab_.lookupFunction(node->callee);
    if (!fn) {
        diags_.error(node->loc, "call to undeclared function '" + node->callee + "'");
        node->inferredType = VoidType::get();
        return;
    }

    if (fn->isKernel) {
        diags_.error(node->loc, "kernel '" + node->callee + "' cannot be called directly from device code");
    }

    if (node->args.size() != fn->paramTypes.size()) {
        diags_.error(node->loc, "function '" + node->callee + "' expects " +
                                std::to_string(fn->paramTypes.size()) + " arguments, got " +
                                std::to_string(node->args.size()));
    }

    for (size_t i = 0; i < node->args.size(); ++i) {
        node->args[i]->accept(this);
        if (i < fn->paramTypes.size()) {
            if (!areTypesCompatible(fn->paramTypes[i].get(), node->args[i]->inferredType.get())) {
                diags_.error(node->args[i]->loc, "argument " + std::to_string(i + 1) +
                             " to '" + node->callee + "' has incompatible type '" +
                             node->args[i]->inferredType->toString() + "', expected '" +
                             fn->paramTypes[i]->toString() + "'");
            }
        }
    }

    node->inferredType = fn->returnType;
}

void TypeChecker::visit(CastExprAST* node) {
    node->expr->accept(this);
    node->inferredType = node->targetType;
}

void TypeChecker::visit(ExprStmtAST* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
}

void TypeChecker::visit(VarDeclStmtAST* node) {
    if (symTab_.isDeclaredInCurrentScope(node->name)) {
        diags_.error(node->loc, "redeclaration of variable '" + node->name + "'");
        return;
    }

    if (node->init) {
        node->init->accept(this);
        if (!areTypesCompatible(node->type.get(), node->init->inferredType.get())) {
            diags_.error(node->loc, "cannot initialize variable of type '" + node->type->toString() +
                                    "' with value of type '" + node->init->inferredType->toString() + "'");
        }
    }

    VarSymbol sym;
    sym.name = node->name;
    sym.type = node->type;
    sym.addressSpace = node->addressSpace;
    sym.loc = node->loc;
    sym.isParam = false;
    sym.isGlobal = false;
    symTab_.insertVar(sym);
}

void TypeChecker::visit(BlockStmtAST* node) {
    symTab_.enterScope();
    for (const auto& stmt : node->statements) {
        stmt->accept(this);
    }
    symTab_.exitScope();
}

void TypeChecker::visit(IfStmtAST* node) {
    node->cond->accept(this);
    if (!node->cond->inferredType->isBool() && !node->cond->inferredType->isInt()) {
        diags_.error(node->cond->loc, "condition in 'if' statement must be boolean or integer, got '" +
                                      node->cond->inferredType->toString() + "'");
    }
    node->thenStmt->accept(this);
    if (node->elseStmt) {
        node->elseStmt->accept(this);
    }
}

void TypeChecker::visit(WhileStmtAST* node) {
    node->cond->accept(this);
    if (!node->cond->inferredType->isBool() && !node->cond->inferredType->isInt()) {
        diags_.error(node->cond->loc, "condition in 'while' statement must be boolean or integer, got '" +
                                      node->cond->inferredType->toString() + "'");
    }
    node->body->accept(this);
}

void TypeChecker::visit(ForStmtAST* node) {
    symTab_.enterScope();
    if (node->init) node->init->accept(this);
    if (node->cond) {
        node->cond->accept(this);
        if (!node->cond->inferredType->isBool() && !node->cond->inferredType->isInt()) {
            diags_.error(node->cond->loc, "condition in 'for' loop must be boolean or integer, got '" +
                                          node->cond->inferredType->toString() + "'");
        }
    }
    if (node->step) node->step->accept(this);
    node->body->accept(this);
    symTab_.exitScope();
}

void TypeChecker::visit(ReturnStmtAST* node) {
    if (!currentFunc_) {
        diags_.error(node->loc, "return statement outside of function");
        return;
    }

    if (currentFunc_->isKernel) {
        if (node->value) {
            diags_.error(node->loc, "kernel function cannot return a value");
        }
        return;
    }

    if (node->value) {
        node->value->accept(this);
        if (!areTypesCompatible(currentFunc_->returnType.get(), node->value->inferredType.get())) {
            diags_.error(node->loc, "return value type '" + node->value->inferredType->toString() +
                                    "' does not match function return type '" +
                                    currentFunc_->returnType->toString() + "'");
        }
    } else {
        if (!currentFunc_->returnType->isVoid()) {
            diags_.error(node->loc, "non-void function must return a value");
        }
    }
}

void TypeChecker::visit(SyncStmtAST* node) {
    if (!currentFunc_ || !currentFunc_->isKernel) {
        diags_.error(node->loc, "'__sync()' is only permitted inside kernel functions");
    }
}

void TypeChecker::visit(FunctionDeclAST* node) {
    currentFunc_ = node;
    symTab_.enterScope();

    for (const auto& param : node->params) {
        VarSymbol sym;
        sym.name = param.name;
        sym.type = param.type;
        sym.addressSpace = AddressSpace::Local;
        if (param.type->isPointer()) {
            sym.addressSpace = static_cast<PointerType*>(param.type.get())->addressSpace;
        }
        sym.loc = param.loc;
        sym.isParam = true;
        symTab_.insertVar(sym);
    }

    if (node->body) {
        for (const auto& s : node->body->statements) {
            s->accept(this);
        }
    }

    symTab_.exitScope();
    currentFunc_ = nullptr;
}

void TypeChecker::visit(GlobalDeclAST* node) {
    VarSymbol sym;
    sym.name = node->name;
    sym.type = node->type;
    sym.addressSpace = node->addressSpace;
    sym.loc = node->loc;
    sym.isGlobal = true;
    symTab_.insertVar(sym);
}

void TypeChecker::visit(ProgramAST* node) {
    // First pass: register all functions and global symbols
    for (const auto& g : node->globals) {
        g->accept(this);
    }

    for (const auto& f : node->functions) {
        FunctionSymbol fn;
        fn.name = f->name;
        fn.isKernel = f->isKernel;
        fn.returnType = f->returnType;
        fn.loc = f->loc;
        for (const auto& p : f->params) {
            fn.paramTypes.push_back(p.type);
        }
        if (!symTab_.insertFunction(fn)) {
            diags_.error(f->loc, "redefinition of function '" + f->name + "'");
        }
    }

    // Second pass: typecheck function bodies
    for (const auto& f : node->functions) {
        f->accept(this);
    }
}

} // namespace kernelforge
