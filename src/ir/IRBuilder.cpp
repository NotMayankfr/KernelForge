#include "IRBuilder.h"

namespace kernelforge::ir {

IRBuilder::IRBuilder(Module* mod) : module_(mod) {}

void IRBuilder::build(ProgramAST* ast) {
    if (!ast) return;
    ast->accept(this);
}

std::string IRBuilder::nextTempName(const std::string& prefix) {
    return prefix + std::to_string(++tempVarCounter_);
}

void IRBuilder::setInsertPoint(BasicBlock* bb) {
    currentBB_ = bb;
}

void IRBuilder::enterScope() {
    scopeStack_.emplace_back();
}

void IRBuilder::exitScope() {
    if (!scopeStack_.empty()) {
        scopeStack_.pop_back();
    }
}

void IRBuilder::declareVar(const std::string& name, Value* val) {
    if (!scopeStack_.empty()) {
        scopeStack_.back()[name] = val;
    }
}

Value* IRBuilder::lookupVar(const std::string& name) {
    for (auto it = scopeStack_.rbegin(); it != scopeStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    // Check module globals
    if (auto* g = module_->getGlobal(name)) return g;
    return nullptr;
}

Value* IRBuilder::createAdd(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("add") : name;
    Opcode op = (lhs->type->isFloat32() || rhs->type->isFloat32()) ? Opcode::FAdd : Opcode::Add;
    auto inst = std::make_unique<BinaryInst>(op, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createSub(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("sub") : name;
    Opcode op = (lhs->type->isFloat32() || rhs->type->isFloat32()) ? Opcode::FSub : Opcode::Sub;
    auto inst = std::make_unique<BinaryInst>(op, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createMul(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("mul") : name;
    Opcode op = (lhs->type->isFloat32() || rhs->type->isFloat32()) ? Opcode::FMul : Opcode::Mul;
    auto inst = std::make_unique<BinaryInst>(op, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createDiv(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("div") : name;
    Opcode op = (lhs->type->isFloat32() || rhs->type->isFloat32()) ? Opcode::FDiv : Opcode::SDiv;
    auto inst = std::make_unique<BinaryInst>(op, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createRem(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("rem") : name;
    auto inst = std::make_unique<BinaryInst>(Opcode::SRem, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createShl(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("shl") : name;
    auto inst = std::make_unique<BinaryInst>(Opcode::Shl, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createShr(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("shr") : name;
    auto inst = std::make_unique<BinaryInst>(Opcode::AShr, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createAnd(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("and") : name;
    auto inst = std::make_unique<BinaryInst>(Opcode::And, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createOr(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("or") : name;
    auto inst = std::make_unique<BinaryInst>(Opcode::Or, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createXor(Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("xor") : name;
    auto inst = std::make_unique<BinaryInst>(Opcode::Xor, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createICmp(CmpPredicate pred, Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("cmp") : name;
    auto inst = std::make_unique<ICmpInst>(pred, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

Value* IRBuilder::createFCmp(CmpPredicate pred, Value* lhs, Value* rhs, const std::string& name) {
    std::string n = name.empty() ? nextTempName("fcmp") : name;
    auto inst = std::make_unique<FCmpInst>(pred, lhs, rhs, n);
    Value* ptr = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ptr;
}

AllocaInst* IRBuilder::createAlloca(std::shared_ptr<IRType> type, AddressSpace space,
                                   size_t arrSize, const std::string& name) {
    std::string n = name.empty() ? nextTempName("alloca") : name;
    auto inst = std::make_unique<AllocaInst>(type, space, arrSize, n);
    AllocaInst* ptr = inst.get();
    // Put allocas in the entry block
    BasicBlock* entry = currentFunc_->getEntryBlock();
    if (entry) {
        entry->insertInstructionAtBeginning(std::move(inst));
    } else {
        currentBB_->addInstruction(std::move(inst));
    }
    return ptr;
}

LoadInst* IRBuilder::createLoad(Value* ptr, const std::string& name) {
    std::string n = name.empty() ? nextTempName("ld") : name;
    auto inst = std::make_unique<LoadInst>(ptr, n);
    LoadInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

StoreInst* IRBuilder::createStore(Value* val, Value* ptr) {
    auto inst = std::make_unique<StoreInst>(val, ptr);
    StoreInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

GetElementPtrInst* IRBuilder::createGEP(Value* ptr, Value* index, const std::string& name) {
    std::string n = name.empty() ? nextTempName("gep") : name;
    auto inst = std::make_unique<GetElementPtrInst>(ptr, index, n);
    GetElementPtrInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

BranchInst* IRBuilder::createBranch(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB) {
    auto inst = std::make_unique<BranchInst>(cond, trueBB, falseBB);
    BranchInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    currentBB_->addSuccessor(trueBB);
    trueBB->addPredecessor(currentBB_);
    currentBB_->addSuccessor(falseBB);
    falseBB->addPredecessor(currentBB_);
    return ret;
}

JumpInst* IRBuilder::createJump(BasicBlock* target) {
    auto inst = std::make_unique<JumpInst>(target);
    JumpInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    currentBB_->addSuccessor(target);
    target->addPredecessor(currentBB_);
    return ret;
}

ReturnInst* IRBuilder::createReturn(Value* val) {
    auto inst = std::make_unique<ReturnInst>(val);
    ReturnInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

CallInst* IRBuilder::createCall(const std::string& callee, std::shared_ptr<IRType> retType,
                               std::vector<Value*> args, const std::string& name) {
    std::string n = name.empty() ? nextTempName("call") : name;
    auto inst = std::make_unique<CallInst>(callee, retType, std::move(args), n);
    CallInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

GetSpecialInst* IRBuilder::createGetSpecial(const std::string& reg, const std::string& name) {
    std::string n = name.empty() ? nextTempName("reg") : name;
    auto inst = std::make_unique<GetSpecialInst>(reg, n);
    GetSpecialInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

SyncInst* IRBuilder::createSync() {
    auto inst = std::make_unique<SyncInst>();
    SyncInst* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

Value* IRBuilder::createCast(Opcode op, Value* src, std::shared_ptr<IRType> destType, const std::string& name) {
    std::string n = name.empty() ? nextTempName("cast") : name;
    auto inst = std::make_unique<CastInst>(op, src, destType, n);
    Value* ret = inst.get();
    currentBB_->addInstruction(std::move(inst));
    return ret;
}

// AST Visitor implementations
void IRBuilder::visit(IntLiteralExprAST* node) {
    static std::vector<std::unique_ptr<ConstantInt>> intPool;
    intPool.push_back(std::make_unique<ConstantInt>(node->value));
    lastExprValue_ = intPool.back().get();
}

void IRBuilder::visit(FloatLiteralExprAST* node) {
    static std::vector<std::unique_ptr<ConstantFloat>> floatPool;
    floatPool.push_back(std::make_unique<ConstantFloat>(node->value));
    lastExprValue_ = floatPool.back().get();
}

void IRBuilder::visit(BoolLiteralExprAST* node) {
    static std::vector<std::unique_ptr<ConstantBool>> boolPool;
    boolPool.push_back(std::make_unique<ConstantBool>(node->value));
    lastExprValue_ = boolPool.back().get();
}

void IRBuilder::visit(VarExprAST* node) {
    Value* slot = lookupVar(node->name);
    if (!slot) {
        lastExprValue_ = nullptr;
        return;
    }
    // Only load from stack alloca slots (local variables). Arguments and Globals are values/pointers themselves.
    if (dynamic_cast<AllocaInst*>(slot)) {
        lastExprValue_ = createLoad(slot, node->name + "_val");
    } else {
        lastExprValue_ = slot;
    }
}

void IRBuilder::visit(BuiltinExprAST* node) {
    std::string regName;
    switch (node->kind) {
        case BuiltinKind::ThreadIdx: regName = "threadIdx."; break;
        case BuiltinKind::BlockIdx:  regName = "blockIdx."; break;
        case BuiltinKind::BlockDim:  regName = "blockDim."; break;
        case BuiltinKind::GridDim:   regName = "gridDim."; break;
    }
    regName.push_back(node->dimension);
    lastExprValue_ = createGetSpecial(regName);
}

Value* IRBuilder::emitLValue(ExprAST* expr) {
    if (auto* v = dynamic_cast<VarExprAST*>(expr)) {
        return lookupVar(v->name);
    }
    if (auto* idx = dynamic_cast<IndexExprAST*>(expr)) {
        Value* baseVal = nullptr;
        if (auto* v = dynamic_cast<VarExprAST*>(idx->base.get())) {
            baseVal = lookupVar(v->name);
            if (baseVal && baseVal->type && baseVal->type->isPointer()) {
                auto* pt = static_cast<PointerIRType*>(baseVal->type.get());
                if (pt->pointee && pt->pointee->isPointer()) {
                    baseVal = createLoad(baseVal, v->name + "_ptr");
                }
            }
        } else {
            idx->base->accept(this);
            baseVal = lastExprValue_;
        }
        idx->index->accept(this);
        Value* indexVal = lastExprValue_;
        return createGEP(baseVal, indexVal, "gep");
    }
    return nullptr;
}

void IRBuilder::visit(BinaryExprAST* node) {
    if (node->op == BinaryOp::Assign) {
        Value* lval = emitLValue(node->lhs.get());
        node->rhs->accept(this);
        Value* rval = lastExprValue_;
        createStore(rval, lval);
        lastExprValue_ = rval;
        return;
    }

    if (node->op == BinaryOp::AddAssign || node->op == BinaryOp::SubAssign ||
        node->op == BinaryOp::MulAssign || node->op == BinaryOp::DivAssign) {
        Value* lval = emitLValue(node->lhs.get());
        Value* curVal = createLoad(lval, "cur");
        node->rhs->accept(this);
        Value* rval = lastExprValue_;
        Value* res = nullptr;
        if (node->op == BinaryOp::AddAssign) res = createAdd(curVal, rval);
        else if (node->op == BinaryOp::SubAssign) res = createSub(curVal, rval);
        else if (node->op == BinaryOp::MulAssign) res = createMul(curVal, rval);
        else if (node->op == BinaryOp::DivAssign) res = createDiv(curVal, rval);
        createStore(res, lval);
        lastExprValue_ = res;
        return;
    }

    // Short-circuit logical AND (&&)
    if (node->op == BinaryOp::LAnd) {
        BasicBlock* lhsBB = currentBB_;
        BasicBlock* rhsBB = currentFunc_->createBasicBlock("land_rhs");
        BasicBlock* mergeBB = currentFunc_->createBasicBlock("land_merge");

        node->lhs->accept(this);
        Value* lhsVal = lastExprValue_;
        createBranch(lhsVal, rhsBB, mergeBB);

        setInsertPoint(rhsBB);
        node->rhs->accept(this);
        Value* rhsVal = lastExprValue_;
        createJump(mergeBB);
        BasicBlock* rhsEndBB = currentBB_;

        setInsertPoint(mergeBB);
        auto phi = std::make_unique<PhiInst>(IRType::getBool(), nextTempName("land"));
        static ConstantBool constFalse(false);
        phi->addIncoming(&constFalse, lhsBB);
        phi->addIncoming(rhsVal, rhsEndBB);
        Value* phiVal = phi.get();
        mergeBB->insertInstructionAtBeginning(std::move(phi));
        lastExprValue_ = phiVal;
        return;
    }

    // Short-circuit logical OR (||)
    if (node->op == BinaryOp::LOr) {
        BasicBlock* lhsBB = currentBB_;
        BasicBlock* rhsBB = currentFunc_->createBasicBlock("lor_rhs");
        BasicBlock* mergeBB = currentFunc_->createBasicBlock("lor_merge");

        node->lhs->accept(this);
        Value* lhsVal = lastExprValue_;
        createBranch(lhsVal, mergeBB, rhsBB);

        setInsertPoint(rhsBB);
        node->rhs->accept(this);
        Value* rhsVal = lastExprValue_;
        createJump(mergeBB);
        BasicBlock* rhsEndBB = currentBB_;

        setInsertPoint(mergeBB);
        auto phi = std::make_unique<PhiInst>(IRType::getBool(), nextTempName("lor"));
        static ConstantBool constTrue(true);
        phi->addIncoming(&constTrue, lhsBB);
        phi->addIncoming(rhsVal, rhsEndBB);
        Value* phiVal = phi.get();
        mergeBB->insertInstructionAtBeginning(std::move(phi));
        lastExprValue_ = phiVal;
        return;
    }

    node->lhs->accept(this);
    Value* l = lastExprValue_;
    node->rhs->accept(this);
    Value* r = lastExprValue_;

    bool isFloat = (l->type->isFloat32() || r->type->isFloat32());

    switch (node->op) {
        case BinaryOp::Add: lastExprValue_ = createAdd(l, r); break;
        case BinaryOp::Sub: lastExprValue_ = createSub(l, r); break;
        case BinaryOp::Mul: lastExprValue_ = createMul(l, r); break;
        case BinaryOp::Div: lastExprValue_ = createDiv(l, r); break;
        case BinaryOp::Mod: lastExprValue_ = createRem(l, r); break;
        case BinaryOp::Shl: lastExprValue_ = createShl(l, r); break;
        case BinaryOp::Shr: lastExprValue_ = createShr(l, r); break;
        case BinaryOp::And: lastExprValue_ = createAnd(l, r); break;
        case BinaryOp::Or:  lastExprValue_ = createOr(l, r); break;
        case BinaryOp::Xor: lastExprValue_ = createXor(l, r); break;
        case BinaryOp::Eq:
            lastExprValue_ = isFloat ? createFCmp(CmpPredicate::EQ, l, r) : createICmp(CmpPredicate::EQ, l, r);
            break;
        case BinaryOp::Ne:
            lastExprValue_ = isFloat ? createFCmp(CmpPredicate::NE, l, r) : createICmp(CmpPredicate::NE, l, r);
            break;
        case BinaryOp::Lt:
            lastExprValue_ = isFloat ? createFCmp(CmpPredicate::OLT, l, r) : createICmp(CmpPredicate::SLT, l, r);
            break;
        case BinaryOp::Le:
            lastExprValue_ = isFloat ? createFCmp(CmpPredicate::OLE, l, r) : createICmp(CmpPredicate::SLE, l, r);
            break;
        case BinaryOp::Gt:
            lastExprValue_ = isFloat ? createFCmp(CmpPredicate::OGT, l, r) : createICmp(CmpPredicate::SGT, l, r);
            break;
        case BinaryOp::Ge:
            lastExprValue_ = isFloat ? createFCmp(CmpPredicate::OGE, l, r) : createICmp(CmpPredicate::SGE, l, r);
            break;
        default: break;
    }
}

void IRBuilder::visit(UnaryExprAST* node) {
    node->operand->accept(this);
    Value* opVal = lastExprValue_;

    if (node->op == UnaryOp::Neg) {
        if (opVal->type->isFloat32()) {
            static ConstantFloat zeroF(0.0f);
            lastExprValue_ = createSub(&zeroF, opVal);
        } else {
            static ConstantInt zeroI(0);
            lastExprValue_ = createSub(&zeroI, opVal);
        }
    } else if (node->op == UnaryOp::Not) {
        static ConstantBool constTrue(true);
        lastExprValue_ = createXor(opVal, &constTrue);
    } else if (node->op == UnaryOp::BitNot) {
        static ConstantInt allOnes(-1);
        lastExprValue_ = createXor(opVal, &allOnes);
    }
}

void IRBuilder::visit(IndexExprAST* node) {
    // If base is a variable, get its address
    Value* baseVal = nullptr;
    if (auto* v = dynamic_cast<VarExprAST*>(node->base.get())) {
        baseVal = lookupVar(v->name);
        if (baseVal && baseVal->type->isPointer()) {
            // If it's a pointer-to-pointer (e.g. alloca storing global float*), load the pointer value
            auto* pt = static_cast<PointerIRType*>(baseVal->type.get());
            if (pt->pointee->isPointer()) {
                baseVal = createLoad(baseVal, v->name + "_ptr");
            }
        }
    } else {
        node->base->accept(this);
        baseVal = lastExprValue_;
    }

    node->index->accept(this);
    Value* idxVal = lastExprValue_;

    Value* elemPtr = createGEP(baseVal, idxVal, "gep");
    lastExprValue_ = createLoad(elemPtr, "elem");
}

void IRBuilder::visit(CallExprAST* node) {
    std::vector<Value*> argVals;
    for (const auto& a : node->args) {
        a->accept(this);
        argVals.push_back(lastExprValue_);
    }
    auto retType = IRType::fromFrontendType(node->inferredType.get());
    lastExprValue_ = createCall(node->callee, retType, std::move(argVals), node->callee + "_res");
}

void IRBuilder::visit(CastExprAST* node) {
    node->expr->accept(this);
    Value* src = lastExprValue_;
    auto destType = IRType::fromFrontendType(node->targetType.get());
    if (src->type->isInt32() && destType->isFloat32()) {
        lastExprValue_ = createCast(Opcode::SIToFP, src, destType);
    } else if (src->type->isFloat32() && destType->isInt32()) {
        lastExprValue_ = createCast(Opcode::FPToSI, src, destType);
    } else {
        lastExprValue_ = src;
    }
}

void IRBuilder::visit(ExprStmtAST* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
}

void IRBuilder::visit(VarDeclStmtAST* node) {
    auto irType = IRType::fromFrontendType(node->type.get());
    AllocaInst* slot = createAlloca(irType, node->addressSpace, node->arraySize, node->name);
    declareVar(node->name, slot);

    if (node->init) {
        node->init->accept(this);
        Value* initVal = lastExprValue_;
        createStore(initVal, slot);
    }
}

void IRBuilder::visit(BlockStmtAST* node) {
    enterScope();
    for (const auto& s : node->statements) {
        if (currentBB_->isTerminated()) break;
        s->accept(this);
    }
    exitScope();
}

void IRBuilder::visit(IfStmtAST* node) {
    BasicBlock* thenBB = currentFunc_->createBasicBlock("if_then");
    BasicBlock* elseBB = node->elseStmt ? currentFunc_->createBasicBlock("if_else") : nullptr;
    BasicBlock* mergeBB = currentFunc_->createBasicBlock("if_end");

    node->cond->accept(this);
    Value* condVal = lastExprValue_;

    createBranch(condVal, thenBB, elseBB ? elseBB : mergeBB);

    // Then block
    setInsertPoint(thenBB);
    node->thenStmt->accept(this);
    if (!currentBB_->isTerminated()) {
        createJump(mergeBB);
    }

    // Else block
    if (elseBB) {
        setInsertPoint(elseBB);
        node->elseStmt->accept(this);
        if (!currentBB_->isTerminated()) {
            createJump(mergeBB);
        }
    }

    // Move mergeBB to the end of basicBlocks so it appears after nested blocks
    auto it = std::find_if(currentFunc_->basicBlocks.begin(), currentFunc_->basicBlocks.end(),
        [mergeBB](const std::unique_ptr<BasicBlock>& b) { return b.get() == mergeBB; });
    if (it != currentFunc_->basicBlocks.end() && it != currentFunc_->basicBlocks.end() - 1) {
        std::unique_ptr<BasicBlock> ptr = std::move(*it);
        currentFunc_->basicBlocks.erase(it);
        currentFunc_->basicBlocks.push_back(std::move(ptr));
    }

    setInsertPoint(mergeBB);
}

void IRBuilder::visit(WhileStmtAST* node) {
    BasicBlock* condBB = currentFunc_->createBasicBlock("while_cond");
    BasicBlock* bodyBB = currentFunc_->createBasicBlock("while_body");
    BasicBlock* exitBB = currentFunc_->createBasicBlock("while_end");

    createJump(condBB);

    setInsertPoint(condBB);
    node->cond->accept(this);
    Value* condVal = lastExprValue_;
    createBranch(condVal, bodyBB, exitBB);

    setInsertPoint(bodyBB);
    node->body->accept(this);
    if (!currentBB_->isTerminated()) {
        createJump(condBB);
    }

    auto it = std::find_if(currentFunc_->basicBlocks.begin(), currentFunc_->basicBlocks.end(),
        [exitBB](const std::unique_ptr<BasicBlock>& b) { return b.get() == exitBB; });
    if (it != currentFunc_->basicBlocks.end() && it != currentFunc_->basicBlocks.end() - 1) {
        std::unique_ptr<BasicBlock> ptr = std::move(*it);
        currentFunc_->basicBlocks.erase(it);
        currentFunc_->basicBlocks.push_back(std::move(ptr));
    }

    setInsertPoint(exitBB);
}

void IRBuilder::visit(ForStmtAST* node) {
    enterScope();
    if (node->init) {
        node->init->accept(this);
    }

    BasicBlock* condBB = currentFunc_->createBasicBlock("for_cond");
    BasicBlock* bodyBB = currentFunc_->createBasicBlock("for_body");
    BasicBlock* stepBB = currentFunc_->createBasicBlock("for_step");
    BasicBlock* exitBB = currentFunc_->createBasicBlock("for_end");

    createJump(condBB);

    setInsertPoint(condBB);
    if (node->cond) {
        node->cond->accept(this);
        Value* condVal = lastExprValue_;
        createBranch(condVal, bodyBB, exitBB);
    } else {
        createJump(bodyBB);
    }

    setInsertPoint(bodyBB);
    node->body->accept(this);
    if (!currentBB_->isTerminated()) {
        createJump(stepBB);
    }

    setInsertPoint(stepBB);
    if (node->step) {
        node->step->accept(this);
    }
    createJump(condBB);

    auto it = std::find_if(currentFunc_->basicBlocks.begin(), currentFunc_->basicBlocks.end(),
        [exitBB](const std::unique_ptr<BasicBlock>& b) { return b.get() == exitBB; });
    if (it != currentFunc_->basicBlocks.end() && it != currentFunc_->basicBlocks.end() - 1) {
        std::unique_ptr<BasicBlock> ptr = std::move(*it);
        currentFunc_->basicBlocks.erase(it);
        currentFunc_->basicBlocks.push_back(std::move(ptr));
    }

    setInsertPoint(exitBB);
    exitScope();
}

void IRBuilder::visit(ReturnStmtAST* node) {
    if (node->value) {
        node->value->accept(this);
        createReturn(lastExprValue_);
    } else {
        createReturn(nullptr);
    }
}

void IRBuilder::visit(SyncStmtAST* node) {
    createSync();
}

void IRBuilder::visit(FunctionDeclAST* node) {
    if (!node->body) {
        // Forward declaration / prototype: do not emit definition
        return;
    }

    auto retType = IRType::fromFrontendType(node->returnType.get());
    currentFunc_ = module_->createFunction(node->name, node->isKernel, retType);

    BasicBlock* entryBB = currentFunc_->createBasicBlock("entry");
    setInsertPoint(entryBB);
    enterScope();

    // Create arguments & allocas for params
    size_t paramIdx = 0;
    for (const auto& p : node->params) {
        auto pType = IRType::fromFrontendType(p.type.get());
        auto arg = std::make_unique<Argument>(pType, p.name, paramIdx++);
        Argument* argPtr = arg.get();
        currentFunc_->arguments.push_back(std::move(arg));

        // Create alloca to store param so it can be mutated
        AllocaInst* slot = createAlloca(pType, AddressSpace::Local, 0, p.name + "_slot");
        createStore(argPtr, slot);
        declareVar(p.name, slot);
    }

    if (node->body) {
        for (const auto& s : node->body->statements) {
            if (currentBB_->isTerminated()) break;
            s->accept(this);
        }
    }

    // If entry/exit block not terminated, add ret void
    if (!currentBB_->isTerminated()) {
        createReturn(nullptr);
    }

    exitScope();
    currentFunc_ = nullptr;
    currentBB_ = nullptr;
}

void IRBuilder::visit(GlobalDeclAST* node) {
    auto irType = IRType::fromFrontendType(node->type.get());
    module_->createGlobal(irType, node->name, node->addressSpace, node->arraySize);
}

void IRBuilder::visit(ProgramAST* node) {
    for (const auto& g : node->globals) {
        g->accept(this);
    }
    for (const auto& f : node->functions) {
        f->accept(this);
    }
}

} // namespace kernelforge::ir
