#include "Parser.h"
#include <iostream>

namespace kernelforge {

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diags)
    : tokens_(std::move(tokens)), diags_(diags) {}

const Token& Parser::peek(size_t offset) const {
    if (current_ + offset >= tokens_.size()) {
        return tokens_.back(); // EOF
    }
    return tokens_[current_ + offset];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::Eof;
}

bool Parser::check(TokenKind kind) const {
    if (isAtEnd()) return kind == TokenKind::Eof;
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::advance() {
    if (!isAtEnd()) current_++;
    return previous();
}

const Token& Parser::consume(TokenKind kind, const std::string& errorMsg) {
    if (check(kind)) return advance();
    diags_.error(peek().loc, errorMsg + " (got '" + peek().text + "')");
    return peek();
}

void Parser::synchronize() {
    advance();
    while (!isAtEnd()) {
        if (previous().kind == TokenKind::Semicolon) return;
        switch (peek().kind) {
            case TokenKind::KwKernel:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwReturn:
            case TokenKind::KwInt:
            case TokenKind::KwFloat:
            case TokenKind::KwBool:
            case TokenKind::KwVoid:
            case TokenKind::KwShared:
            case TokenKind::KwGlobal:
                return;
            default:
                break;
        }
        advance();
    }
}

bool Parser::isTypeStart(const Token& tok) const {
    return tok.kind == TokenKind::KwInt ||
           tok.kind == TokenKind::KwFloat ||
           tok.kind == TokenKind::KwBool ||
           tok.kind == TokenKind::KwVoid ||
           tok.kind == TokenKind::KwGlobal ||
           tok.kind == TokenKind::KwShared ||
           tok.kind == TokenKind::KwLocal ||
           tok.kind == TokenKind::KwConst;
}

std::shared_ptr<Type> Parser::parseType(AddressSpace& outSpace) {
    outSpace = AddressSpace::Generic;

    if (match(TokenKind::KwConst)) {
        // Ignored for now
    }

    if (match(TokenKind::KwGlobal)) {
        outSpace = AddressSpace::Global;
    } else if (match(TokenKind::KwShared)) {
        outSpace = AddressSpace::Shared;
    } else if (match(TokenKind::KwLocal)) {
        outSpace = AddressSpace::Local;
    }

    std::shared_ptr<Type> baseType;
    if (match(TokenKind::KwInt)) {
        baseType = IntType::get();
    } else if (match(TokenKind::KwFloat)) {
        baseType = FloatType::get();
    } else if (match(TokenKind::KwBool)) {
        baseType = BoolType::get();
    } else if (match(TokenKind::KwVoid)) {
        baseType = VoidType::get();
    } else {
        diags_.error(peek().loc, "expected type name");
        return nullptr;
    }

    // Check pointer qualifiers
    while (match(TokenKind::Star)) {
        baseType = std::make_shared<PointerType>(baseType, outSpace);
    }

    return baseType;
}

std::unique_ptr<ProgramAST> Parser::parseProgram() {
    auto program = std::make_unique<ProgramAST>(peek().loc);

    while (!isAtEnd()) {
        try {
            if (check(TokenKind::KwKernel)) {
                advance(); // consume 'kernel'
                program->functions.push_back(parseFunctionDecl(true));
            } else if (check(TokenKind::KwShared)) {
                // Global shared array declaration
                AddressSpace space = AddressSpace::Shared;
                auto type = parseType(space);
                program->globals.push_back(parseGlobalDecl(space, type));
            } else if (isTypeStart(peek())) {
                AddressSpace space = AddressSpace::Generic;
                auto type = parseType(space);
                if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::LParen) {
                    std::string name = consume(TokenKind::Identifier, "expected function name").text;
                    consume(TokenKind::LParen, "expected '(' after function name");
                    std::vector<ParamDecl> params;
                    if (!check(TokenKind::RParen)) {
                        do {
                            AddressSpace pSpace = AddressSpace::Generic;
                            auto pType = parseType(pSpace);
                            std::string pName = consume(TokenKind::Identifier, "expected parameter name").text;
                            params.push_back({peek().loc, pType, pName});
                        } while (match(TokenKind::Comma));
                    }
                    consume(TokenKind::RParen, "expected ')' after parameters");
                    std::unique_ptr<BlockStmtAST> body = nullptr;
                    if (match(TokenKind::Semicolon)) {
                        // Function prototype / forward declaration
                    } else {
                        body = parseBlock();
                    }
                    program->functions.push_back(std::make_unique<FunctionDeclAST>(
                        peek().loc, false, type, name, std::move(params), std::move(body)));
                } else {
                    program->globals.push_back(parseGlobalDecl(space, type));
                }
            } else {
                diags_.error(peek().loc, "unexpected top-level token: " + peek().text);
                synchronize();
            }
        } catch (...) {
            synchronize();
        }
    }

    return program;
}

std::unique_ptr<GlobalDeclAST> Parser::parseGlobalDecl(AddressSpace space, std::shared_ptr<Type> type) {
    SourceLoc loc = peek().loc;
    std::string name = consume(TokenKind::Identifier, "expected variable name").text;
    size_t arraySize = 0;
    if (match(TokenKind::LBracket)) {
        const Token& szTok = consume(TokenKind::IntLiteral, "expected array size");
        arraySize = static_cast<size_t>(szTok.intVal);
        consume(TokenKind::RBracket, "expected ']'");
        type = std::make_shared<ArrayType>(type, arraySize, space);
    }
    consume(TokenKind::Semicolon, "expected ';' after global declaration");
    return std::make_unique<GlobalDeclAST>(loc, type, name, space, arraySize);
}

std::unique_ptr<FunctionDeclAST> Parser::parseFunctionDecl(bool isKernel) {
    SourceLoc loc = previous().loc;
    std::shared_ptr<Type> retType = VoidType::get();

    std::string name = consume(TokenKind::Identifier, "expected kernel/function name").text;
    consume(TokenKind::LParen, "expected '(' after name");

    std::vector<ParamDecl> params;
    if (!check(TokenKind::RParen)) {
        do {
            AddressSpace pSpace = AddressSpace::Generic;
            auto pType = parseType(pSpace);
            std::string pName = consume(TokenKind::Identifier, "expected parameter name").text;
            params.push_back({peek().loc, pType, pName});
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "expected ')' after parameters");

    auto body = parseBlock();
    return std::make_unique<FunctionDeclAST>(loc, isKernel, retType, name, std::move(params), std::move(body));
}

std::unique_ptr<StmtAST> Parser::parseStatement() {
    if (check(TokenKind::LBrace)) {
        return parseBlock();
    }
    if (check(TokenKind::KwIf)) {
        return parseIf();
    }
    if (check(TokenKind::KwWhile)) {
        return parseWhile();
    }
    if (check(TokenKind::KwFor)) {
        return parseFor();
    }
    if (check(TokenKind::KwReturn)) {
        return parseReturn();
    }
    if (check(TokenKind::KwSync)) {
        return parseSync();
    }
    if (isTypeStart(peek())) {
        AddressSpace space = AddressSpace::Local;
        auto type = parseType(space);
        return parseVarDeclStmt(type, space);
    }
    return parseExprOrAssignStmt();
}

std::unique_ptr<BlockStmtAST> Parser::parseBlock() {
    SourceLoc loc = peek().loc;
    consume(TokenKind::LBrace, "expected '{'");
    std::vector<std::unique_ptr<StmtAST>> stmts;
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        auto s = parseStatement();
        if (s) stmts.push_back(std::move(s));
    }
    consume(TokenKind::RBrace, "expected '}'");
    return std::make_unique<BlockStmtAST>(loc, std::move(stmts));
}

std::unique_ptr<StmtAST> Parser::parseVarDeclStmt(std::shared_ptr<Type> type, AddressSpace space) {
    SourceLoc loc = peek().loc;
    std::string name = consume(TokenKind::Identifier, "expected variable name").text;

    size_t arraySize = 0;
    if (match(TokenKind::LBracket)) {
        const Token& szTok = consume(TokenKind::IntLiteral, "expected array size");
        arraySize = static_cast<size_t>(szTok.intVal);
        consume(TokenKind::RBracket, "expected ']'");
        type = std::make_shared<ArrayType>(type, arraySize, space);
    }

    std::unique_ptr<ExprAST> init = nullptr;
    if (match(TokenKind::Equal)) {
        init = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after variable declaration");
    return std::make_unique<VarDeclStmtAST>(loc, type, name, std::move(init), space, arraySize);
}

std::unique_ptr<IfStmtAST> Parser::parseIf() {
    SourceLoc loc = consume(TokenKind::KwIf, "expected 'if'").loc;
    consume(TokenKind::LParen, "expected '(' after 'if'");
    auto cond = parseExpression();
    consume(TokenKind::RParen, "expected ')' after condition");

    auto thenStmt = parseStatement();
    std::unique_ptr<StmtAST> elseStmt = nullptr;
    if (match(TokenKind::KwElse)) {
        elseStmt = parseStatement();
    }
    return std::make_unique<IfStmtAST>(loc, std::move(cond), std::move(thenStmt), std::move(elseStmt));
}

std::unique_ptr<WhileStmtAST> Parser::parseWhile() {
    SourceLoc loc = consume(TokenKind::KwWhile, "expected 'while'").loc;
    consume(TokenKind::LParen, "expected '(' after 'while'");
    auto cond = parseExpression();
    consume(TokenKind::RParen, "expected ')' after condition");
    auto body = parseStatement();
    return std::make_unique<WhileStmtAST>(loc, std::move(cond), std::move(body));
}

std::unique_ptr<ForStmtAST> Parser::parseFor() {
    SourceLoc loc = consume(TokenKind::KwFor, "expected 'for'").loc;
    consume(TokenKind::LParen, "expected '(' after 'for'");

    std::unique_ptr<StmtAST> init = nullptr;
    if (!check(TokenKind::Semicolon)) {
        if (isTypeStart(peek())) {
            AddressSpace space = AddressSpace::Local;
            auto type = parseType(space);
            init = parseVarDeclStmt(type, space);
        } else {
            init = parseExprOrAssignStmt();
        }
    } else {
        advance(); // consume ';'
    }

    std::unique_ptr<ExprAST> cond = nullptr;
    if (!check(TokenKind::Semicolon)) {
        cond = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after for-loop condition");

    std::unique_ptr<ExprAST> step = nullptr;
    if (!check(TokenKind::RParen)) {
        step = parseExpression();
    }
    consume(TokenKind::RParen, "expected ')' after for clauses");

    auto body = parseStatement();
    return std::make_unique<ForStmtAST>(loc, std::move(init), std::move(cond), std::move(step), std::move(body));
}

std::unique_ptr<ReturnStmtAST> Parser::parseReturn() {
    SourceLoc loc = consume(TokenKind::KwReturn, "expected 'return'").loc;
    std::unique_ptr<ExprAST> val = nullptr;
    if (!check(TokenKind::Semicolon)) {
        val = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after return statement");
    return std::make_unique<ReturnStmtAST>(loc, std::move(val));
}

std::unique_ptr<SyncStmtAST> Parser::parseSync() {
    SourceLoc loc = consume(TokenKind::KwSync, "expected '__sync'").loc;
    consume(TokenKind::LParen, "expected '(' after '__sync'");
    consume(TokenKind::RParen, "expected ')' after '(' in '__sync()'");
    consume(TokenKind::Semicolon, "expected ';' after '__sync()'");
    return std::make_unique<SyncStmtAST>(loc);
}

std::unique_ptr<StmtAST> Parser::parseExprOrAssignStmt() {
    SourceLoc loc = peek().loc;
    auto expr = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after expression");
    return std::make_unique<ExprStmtAST>(loc, std::move(expr));
}

std::unique_ptr<ExprAST> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<ExprAST> Parser::parseAssignment() {
    auto expr = parseLogicalOr();

    if (match(TokenKind::Equal)) {
        SourceLoc loc = previous().loc;
        auto val = parseAssignment();
        return std::make_unique<BinaryExprAST>(loc, BinaryOp::Assign, std::move(expr), std::move(val));
    }
    if (match(TokenKind::PlusEqual)) {
        SourceLoc loc = previous().loc;
        auto val = parseAssignment();
        return std::make_unique<BinaryExprAST>(loc, BinaryOp::AddAssign, std::move(expr), std::move(val));
    }
    if (match(TokenKind::MinusEqual)) {
        SourceLoc loc = previous().loc;
        auto val = parseAssignment();
        return std::make_unique<BinaryExprAST>(loc, BinaryOp::SubAssign, std::move(expr), std::move(val));
    }
    if (match(TokenKind::StarEqual)) {
        SourceLoc loc = previous().loc;
        auto val = parseAssignment();
        return std::make_unique<BinaryExprAST>(loc, BinaryOp::MulAssign, std::move(expr), std::move(val));
    }
    if (match(TokenKind::SlashEqual)) {
        SourceLoc loc = previous().loc;
        auto val = parseAssignment();
        return std::make_unique<BinaryExprAST>(loc, BinaryOp::DivAssign, std::move(expr), std::move(val));
    }

    return expr;
}

std::unique_ptr<ExprAST> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    while (match(TokenKind::PipePipe)) {
        SourceLoc loc = previous().loc;
        auto right = parseLogicalAnd();
        expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::LOr, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseLogicalAnd() {
    auto expr = parseBitwiseOr();
    while (match(TokenKind::AmpAmp)) {
        SourceLoc loc = previous().loc;
        auto right = parseBitwiseOr();
        expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::LAnd, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();
    while (match(TokenKind::Pipe)) {
        SourceLoc loc = previous().loc;
        auto right = parseBitwiseXor();
        expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();
    while (match(TokenKind::Caret)) {
        SourceLoc loc = previous().loc;
        auto right = parseBitwiseAnd();
        expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Xor, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseBitwiseAnd() {
    auto expr = parseEquality();
    while (match(TokenKind::Amp)) {
        SourceLoc loc = previous().loc;
        auto right = parseEquality();
        expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseEquality() {
    auto expr = parseRelational();
    while (true) {
        if (match(TokenKind::EqualEqual)) {
            SourceLoc loc = previous().loc;
            auto right = parseRelational();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Eq, std::move(expr), std::move(right));
        } else if (match(TokenKind::BangEqual)) {
            SourceLoc loc = previous().loc;
            auto right = parseRelational();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Ne, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseRelational() {
    auto expr = parseShift();
    while (true) {
        if (match(TokenKind::Less)) {
            SourceLoc loc = previous().loc;
            auto right = parseShift();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Lt, std::move(expr), std::move(right));
        } else if (match(TokenKind::LessEqual)) {
            SourceLoc loc = previous().loc;
            auto right = parseShift();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Le, std::move(expr), std::move(right));
        } else if (match(TokenKind::Greater)) {
            SourceLoc loc = previous().loc;
            auto right = parseShift();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Gt, std::move(expr), std::move(right));
        } else if (match(TokenKind::GreaterEqual)) {
            SourceLoc loc = previous().loc;
            auto right = parseShift();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Ge, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseShift() {
    auto expr = parseAdditive();
    while (true) {
        if (match(TokenKind::LessLess)) {
            SourceLoc loc = previous().loc;
            auto right = parseAdditive();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Shl, std::move(expr), std::move(right));
        } else if (match(TokenKind::GreaterGreater)) {
            SourceLoc loc = previous().loc;
            auto right = parseAdditive();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Shr, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseAdditive() {
    auto expr = parseMultiplicative();
    while (true) {
        if (match(TokenKind::Plus)) {
            SourceLoc loc = previous().loc;
            auto right = parseMultiplicative();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Add, std::move(expr), std::move(right));
        } else if (match(TokenKind::Minus)) {
            SourceLoc loc = previous().loc;
            auto right = parseMultiplicative();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Sub, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseMultiplicative() {
    auto expr = parseUnary();
    while (true) {
        if (match(TokenKind::Star)) {
            SourceLoc loc = previous().loc;
            auto right = parseUnary();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Mul, std::move(expr), std::move(right));
        } else if (match(TokenKind::Slash)) {
            SourceLoc loc = previous().loc;
            auto right = parseUnary();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Div, std::move(expr), std::move(right));
        } else if (match(TokenKind::Percent)) {
            SourceLoc loc = previous().loc;
            auto right = parseUnary();
            expr = std::make_unique<BinaryExprAST>(loc, BinaryOp::Mod, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseUnary() {
    if (match(TokenKind::Minus)) {
        SourceLoc loc = previous().loc;
        auto operand = parseUnary();
        return std::make_unique<UnaryExprAST>(loc, UnaryOp::Neg, std::move(operand));
    }
    if (match(TokenKind::Bang)) {
        SourceLoc loc = previous().loc;
        auto operand = parseUnary();
        return std::make_unique<UnaryExprAST>(loc, UnaryOp::Not, std::move(operand));
    }
    if (match(TokenKind::Tilde)) {
        SourceLoc loc = previous().loc;
        auto operand = parseUnary();
        return std::make_unique<UnaryExprAST>(loc, UnaryOp::BitNot, std::move(operand));
    }
    return parsePostfix();
}

std::unique_ptr<ExprAST> Parser::parsePostfix() {
    auto expr = parsePrimary();
    while (true) {
        if (match(TokenKind::LBracket)) {
            SourceLoc loc = previous().loc;
            auto index = parseExpression();
            consume(TokenKind::RBracket, "expected ']' after index");
            expr = std::make_unique<IndexExprAST>(loc, std::move(expr), std::move(index));
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parsePrimary() {
    SourceLoc loc = peek().loc;

    if (match(TokenKind::IntLiteral)) {
        return std::make_unique<IntLiteralExprAST>(loc, previous().intVal);
    }
    if (match(TokenKind::FloatLiteral)) {
        return std::make_unique<FloatLiteralExprAST>(loc, previous().floatVal);
    }
    if (match(TokenKind::KwTrue)) {
        return std::make_unique<BoolLiteralExprAST>(loc, true);
    }
    if (match(TokenKind::KwFalse)) {
        return std::make_unique<BoolLiteralExprAST>(loc, false);
    }

    // Builtins: threadIdx.x, blockIdx.x, blockDim.x, gridDim.x
    if (match(TokenKind::BuiltinThreadIdx) ||
        match(TokenKind::BuiltinBlockIdx) ||
        match(TokenKind::BuiltinBlockDim) ||
        match(TokenKind::BuiltinGridDim)) {
        TokenKind bKind = previous().kind;
        consume(TokenKind::Dot, "expected '.' after GPU builtin");
        const Token& dimTok = consume(TokenKind::Identifier, "expected dimension 'x', 'y', or 'z'");
        char dim = 'x';
        if (dimTok.text == "x") dim = 'x';
        else if (dimTok.text == "y") dim = 'y';
        else if (dimTok.text == "z") dim = 'z';
        else diags_.error(dimTok.loc, "invalid builtin dimension '" + dimTok.text + "', expected x, y, or z");

        BuiltinKind kind = BuiltinKind::ThreadIdx;
        if (bKind == TokenKind::BuiltinBlockIdx) kind = BuiltinKind::BlockIdx;
        else if (bKind == TokenKind::BuiltinBlockDim) kind = BuiltinKind::BlockDim;
        else if (bKind == TokenKind::BuiltinGridDim) kind = BuiltinKind::GridDim;

        return std::make_unique<BuiltinExprAST>(loc, kind, dim);
    }

    if (match(TokenKind::Identifier)) {
        std::string name = previous().text;
        // Function call: name(args...)
        if (match(TokenKind::LParen)) {
            std::vector<std::unique_ptr<ExprAST>> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "expected ')' after call arguments");
            return std::make_unique<CallExprAST>(loc, name, std::move(args));
        }
        return std::make_unique<VarExprAST>(loc, name);
    }

    if (match(TokenKind::LParen)) {
        auto expr = parseExpression();
        consume(TokenKind::RParen, "expected ')'");
        return expr;
    }

    diags_.error(loc, "expected expression, got '" + peek().text + "'");
    advance();
    return std::make_unique<IntLiteralExprAST>(loc, 0);
}

} // namespace kernelforge
