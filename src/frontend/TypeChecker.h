#pragma once

#include "AST.h"
#include "SymbolTable.h"
#include "common/Diagnostic.h"

namespace kernelforge {

class TypeChecker : public ASTVisitor {
public:
    TypeChecker(DiagnosticEngine& diags);

    bool check(ProgramAST* program);

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

    const SymbolTable& symbolTable() const { return symTab_; }

private:
    DiagnosticEngine& diags_;
    SymbolTable symTab_;
    FunctionDeclAST* currentFunc_ = nullptr;

    bool areTypesCompatible(const Type* expected, const Type* actual);
    bool checkLValue(const ExprAST* expr);
};

} // namespace kernelforge
