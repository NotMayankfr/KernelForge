#pragma once

#include "Module.h"
#include "frontend/AST.h"
#include <unordered_map>
#include <string>

namespace kernelforge::ir {

class IRBuilder : public ASTVisitor {
public:
    IRBuilder(Module* mod);

    void build(ProgramAST* ast);

    void visit(IntLiteralExprAST* node) override;
    void visit(FloatLiteralExprAST* node) override;
    void visit(BoolLiteralExprAST* node) override;
    void visit(VarExprAST* node) override;
    void visit(BuiltinExprAST* node) override;
    void visit(BinaryExprAST* node) override;
    void visit(UnaryExprAST* node) override;
    void visit(IndexExprAST* node) override;
    void visit(CallExprAST* node) override;
    void visit(CastExprAST* node) override;

    void visit(ExprStmtAST* node) override;
    void visit(VarDeclStmtAST* node) override;
    void visit(BlockStmtAST* node) override;
    void visit(IfStmtAST* node) override;
    void visit(WhileStmtAST* node) override;
    void visit(ForStmtAST* node) override;
    void visit(ReturnStmtAST* node) override;
    void visit(SyncStmtAST* node) override;

    void visit(FunctionDeclAST* node) override;
    void visit(GlobalDeclAST* node) override;
    void visit(ProgramAST* node) override;

    // Helper methods to emit instructions
    void setInsertPoint(BasicBlock* bb);
    BasicBlock* getInsertPoint() const { return currentBB_; }

    Value* createAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSub(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createMul(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createDiv(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createRem(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createShl(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createShr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createAnd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createOr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createXor(Value* lhs, Value* rhs, const std::string& name = "");

    Value* createICmp(CmpPredicate pred, Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmp(CmpPredicate pred, Value* lhs, Value* rhs, const std::string& name = "");

    AllocaInst* createAlloca(std::shared_ptr<IRType> type, AddressSpace space = AddressSpace::Local,
                            size_t arrSize = 0, const std::string& name = "");
    LoadInst* createLoad(Value* ptr, const std::string& name = "");
    StoreInst* createStore(Value* val, Value* ptr);
    GetElementPtrInst* createGEP(Value* ptr, Value* index, const std::string& name = "");

    BranchInst* createBranch(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB);
    JumpInst* createJump(BasicBlock* target);
    ReturnInst* createReturn(Value* val = nullptr);
    CallInst* createCall(const std::string& callee, std::shared_ptr<IRType> retType,
                         std::vector<Value*> args, const std::string& name = "");
    GetSpecialInst* createGetSpecial(const std::string& reg, const std::string& name = "");
    SyncInst* createSync();

    Value* createCast(Opcode op, Value* src, std::shared_ptr<IRType> destType, const std::string& name = "");

private:
    Module* module_;
    Function* currentFunc_ = nullptr;
    BasicBlock* currentBB_ = nullptr;

    // Scoped variable map: maps variable name to its AllocaInst or GlobalVariable
    std::vector<std::unordered_map<std::string, Value*>> scopeStack_;

    Value* lastExprValue_ = nullptr;
    size_t tempVarCounter_ = 0;

    std::string nextTempName(const std::string& prefix = "v");
    void enterScope();
    void exitScope();
    void declareVar(const std::string& name, Value* val);
    Value* lookupVar(const std::string& name);

    Value* emitLValue(ExprAST* expr);
};

} // namespace kernelforge::ir
