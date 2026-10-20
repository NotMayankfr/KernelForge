#include "Token.h"

namespace kernelforge {

const char* tokenKindToString(TokenKind kind) {
    switch (kind) {
        case TokenKind::Eof: return "EOF";
        case TokenKind::Invalid: return "invalid";
        case TokenKind::KwKernel: return "kernel";
        case TokenKind::KwGlobal: return "global";
        case TokenKind::KwShared: return "shared";
        case TokenKind::KwLocal: return "local";
        case TokenKind::KwConst: return "const";
        case TokenKind::KwInt: return "int";
        case TokenKind::KwFloat: return "float";
        case TokenKind::KwBool: return "bool";
        case TokenKind::KwVoid: return "void";
        case TokenKind::KwIf: return "if";
        case TokenKind::KwElse: return "else";
        case TokenKind::KwWhile: return "while";
        case TokenKind::KwFor: return "for";
        case TokenKind::KwReturn: return "return";
        case TokenKind::KwTrue: return "true";
        case TokenKind::KwFalse: return "false";
        case TokenKind::KwSync: return "__sync";
        case TokenKind::BuiltinThreadIdx: return "threadIdx";
        case TokenKind::BuiltinBlockIdx: return "blockIdx";
        case TokenKind::BuiltinBlockDim: return "blockDim";
        case TokenKind::BuiltinGridDim: return "gridDim";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::IntLiteral: return "int literal";
        case TokenKind::FloatLiteral: return "float literal";
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Equal: return "=";
        case TokenKind::PlusEqual: return "+=";
        case TokenKind::MinusEqual: return "-=";
        case TokenKind::StarEqual: return "*=";
        case TokenKind::SlashEqual: return "/=";
        case TokenKind::EqualEqual: return "==";
        case TokenKind::BangEqual: return "!=";
        case TokenKind::Less: return "<";
        case TokenKind::LessEqual: return "<=";
        case TokenKind::Greater: return ">";
        case TokenKind::GreaterEqual: return ">=";
        case TokenKind::AmpAmp: return "&&";
        case TokenKind::PipePipe: return "||";
        case TokenKind::Bang: return "!";
        case TokenKind::Amp: return "&";
        case TokenKind::Pipe: return "|";
        case TokenKind::Caret: return "^";
        case TokenKind::Tilde: return "~";
        case TokenKind::LessLess: return "<<";
        case TokenKind::GreaterGreater: return ">>";
        case TokenKind::Dot: return ".";
        case TokenKind::Comma: return ",";
        case TokenKind::Semicolon: return ";";
        case TokenKind::LParen: return "(";
        case TokenKind::RParen: return ")";
        case TokenKind::LBrace: return "{";
        case TokenKind::RBrace: return "}";
        case TokenKind::LBracket: return "[";
        case TokenKind::RBracket: return "]";
    }
    return "unknown";
}

std::string Token::toString() const {
    if (kind == TokenKind::Identifier || kind == TokenKind::IntLiteral || kind == TokenKind::FloatLiteral) {
        return std::string(tokenKindToString(kind)) + "('" + text + "')";
    }
    return tokenKindToString(kind);
}

} // namespace kernelforge
