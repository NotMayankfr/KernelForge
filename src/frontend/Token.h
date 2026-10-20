#pragma once

#include "common/SourceLoc.h"
#include <string>
#include <string_view>

namespace kernelforge {

enum class TokenKind {
    // End of file / Invalid
    Eof,
    Invalid,

    // Keywords
    KwKernel,
    KwGlobal,
    KwShared,
    KwLocal,
    KwConst,
    KwInt,
    KwFloat,
    KwBool,
    KwVoid,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwReturn,
    KwTrue,
    KwFalse,
    KwSync,

    // Builtins
    BuiltinThreadIdx,
    BuiltinBlockIdx,
    BuiltinBlockDim,
    BuiltinGridDim,

    // Literals
    Identifier,
    IntLiteral,
    FloatLiteral,

    // Operators & Punctuation
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Percent,        // %
    Equal,          // =
    PlusEqual,      // +=
    MinusEqual,     // -=
    StarEqual,      // *=
    SlashEqual,     // /=
    EqualEqual,     // ==
    BangEqual,      // !=
    Less,           // <
    LessEqual,      // <=
    Greater,        // >
    GreaterEqual,   // >=
    AmpAmp,         // &&
    PipePipe,       // ||
    Bang,           // !
    Amp,            // &
    Pipe,           // |
    Caret,          // ^
    Tilde,          // ~
    LessLess,       // <<
    GreaterGreater, // >>

    Dot,            // .
    Comma,          // ,
    Semicolon,      // ;
    LParen,         // (
    RParen,         // )
    LBrace,         // {
    RBrace,         // }
    LBracket,       // [
    RBracket        // ]
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    int intVal = 0;
    float floatVal = 0.0f;
    SourceLoc loc;

    bool is(TokenKind k) const { return kind == k; }
    bool isNot(TokenKind k) const { return kind != k; }

    std::string toString() const;
};

const char* tokenKindToString(TokenKind kind);

} // namespace kernelforge
