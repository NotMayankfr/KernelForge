#pragma once

#include "Token.h"
#include "AST.h"
#include "common/Diagnostic.h"
#include <vector>
#include <memory>

namespace kernelforge {

class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticEngine& diags);

    std::unique_ptr<ProgramAST> parseProgram();

private:
    std::vector<Token> tokens_;
    DiagnosticEngine& diags_;
    size_t current_ = 0;

    const Token& peek(size_t offset = 0) const;
    const Token& previous() const;
    bool isAtEnd() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& advance();
    const Token& consume(TokenKind kind, const std::string& errorMsg);

    // Parsing routines
    std::shared_ptr<Type> parseType(AddressSpace& outSpace);
    std::unique_ptr<FunctionDeclAST> parseFunctionDecl(bool isKernel);
    std::unique_ptr<GlobalDeclAST> parseGlobalDecl(AddressSpace space, std::shared_ptr<Type> type);

    std::unique_ptr<StmtAST> parseStatement();
    std::unique_ptr<StmtAST> parseVarDeclStmt(std::shared_ptr<Type> type, AddressSpace space);
    std::unique_ptr<BlockStmtAST> parseBlock();
    std::unique_ptr<IfStmtAST> parseIf();
    std::unique_ptr<WhileStmtAST> parseWhile();
    std::unique_ptr<ForStmtAST> parseFor();
    std::unique_ptr<ReturnStmtAST> parseReturn();
    std::unique_ptr<SyncStmtAST> parseSync();
    std::unique_ptr<StmtAST> parseExprOrAssignStmt();

    // Expression parsing with precedence climbing
    std::unique_ptr<ExprAST> parseExpression();
    std::unique_ptr<ExprAST> parseAssignment();
    std::unique_ptr<ExprAST> parseLogicalOr();
    std::unique_ptr<ExprAST> parseLogicalAnd();
    std::unique_ptr<ExprAST> parseBitwiseOr();
    std::unique_ptr<ExprAST> parseBitwiseXor();
    std::unique_ptr<ExprAST> parseBitwiseAnd();
    std::unique_ptr<ExprAST> parseEquality();
    std::unique_ptr<ExprAST> parseRelational();
    std::unique_ptr<ExprAST> parseShift();
    std::unique_ptr<ExprAST> parseAdditive();
    std::unique_ptr<ExprAST> parseMultiplicative();
    std::unique_ptr<ExprAST> parseUnary();
    std::unique_ptr<ExprAST> parsePostfix();
    std::unique_ptr<ExprAST> parsePrimary();

    void synchronize();
    bool isTypeStart(const Token& tok) const;
};

} // namespace kernelforge
