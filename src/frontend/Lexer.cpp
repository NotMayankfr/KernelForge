#include "Lexer.h"
#include <cctype>
#include <cstdlib>

namespace kernelforge {

Lexer::Lexer(std::string source, std::string filename, DiagnosticEngine& diags)
    : source_(std::move(source)), filename_(std::move(filename)), diags_(diags) {
    diags_.setSource(source_);
}

char Lexer::peek(size_t offset) const {
    if (pos_ + offset >= source_.size()) return '\0';
    return source_[pos_ + offset];
}

char Lexer::advance() {
    if (pos_ >= source_.size()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

SourceLoc Lexer::currentLoc() const {
    return SourceLoc(filename_, line_, col_);
}

void Lexer::skipWhitespaceAndComments() {
    while (pos_ < source_.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            // Line comment
            advance(); advance();
            while (peek() != '\0' && peek() != '\n') {
                advance();
            }
        } else if (c == '/' && peek(1) == '*') {
            // Block comment
            SourceLoc commentStart = currentLoc();
            advance(); advance();
            bool closed = false;
            while (peek() != '\0') {
                if (peek() == '*' && peek(1) == '/') {
                    advance(); advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                diags_.error(commentStart, "unterminated block comment");
            }
        } else {
            break;
        }
    }
}

Token Lexer::lexIdentifierOrKeyword(const SourceLoc& startLoc) {
    std::string text;
    while (std::isalnum(peek()) || peek() == '_') {
        text.push_back(advance());
    }

    Token tok;
    tok.loc = startLoc;
    tok.text = text;

    if (text == "kernel") tok.kind = TokenKind::KwKernel;
    else if (text == "global") tok.kind = TokenKind::KwGlobal;
    else if (text == "shared") tok.kind = TokenKind::KwShared;
    else if (text == "local") tok.kind = TokenKind::KwLocal;
    else if (text == "const") tok.kind = TokenKind::KwConst;
    else if (text == "int") tok.kind = TokenKind::KwInt;
    else if (text == "float") tok.kind = TokenKind::KwFloat;
    else if (text == "bool") tok.kind = TokenKind::KwBool;
    else if (text == "void") tok.kind = TokenKind::KwVoid;
    else if (text == "if") tok.kind = TokenKind::KwIf;
    else if (text == "else") tok.kind = TokenKind::KwElse;
    else if (text == "while") tok.kind = TokenKind::KwWhile;
    else if (text == "for") tok.kind = TokenKind::KwFor;
    else if (text == "return") tok.kind = TokenKind::KwReturn;
    else if (text == "true") {
        tok.kind = TokenKind::KwTrue;
        tok.intVal = 1;
    }
    else if (text == "false") {
        tok.kind = TokenKind::KwFalse;
        tok.intVal = 0;
    }
    else if (text == "__sync") tok.kind = TokenKind::KwSync;
    else if (text == "threadIdx") tok.kind = TokenKind::BuiltinThreadIdx;
    else if (text == "blockIdx") tok.kind = TokenKind::BuiltinBlockIdx;
    else if (text == "blockDim") tok.kind = TokenKind::BuiltinBlockDim;
    else if (text == "gridDim") tok.kind = TokenKind::BuiltinGridDim;
    else {
        tok.kind = TokenKind::Identifier;
    }

    return tok;
}

Token Lexer::lexNumber(const SourceLoc& startLoc) {
    std::string text;
    bool isFloat = false;

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        text.push_back(advance());
        text.push_back(advance());
        while (std::isxdigit(peek())) {
            text.push_back(advance());
        }
        int val = static_cast<int>(std::strtol(text.c_str(), nullptr, 16));
        Token tok;
        tok.kind = TokenKind::IntLiteral;
        tok.text = text;
        tok.intVal = val;
        tok.loc = startLoc;
        return tok;
    }

    while (std::isdigit(peek())) {
        text.push_back(advance());
    }

    if (peek() == '.' && std::isdigit(peek(1))) {
        isFloat = true;
        text.push_back(advance()); // '.'
        while (std::isdigit(peek())) {
            text.push_back(advance());
        }
    }

    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        text.push_back(advance());
        if (peek() == '+' || peek() == '-') {
            text.push_back(advance());
        }
        while (std::isdigit(peek())) {
            text.push_back(advance());
        }
    }

    if (peek() == 'f' || peek() == 'F') {
        isFloat = true;
        text.push_back(advance());
    }

    Token tok;
    tok.text = text;
    tok.loc = startLoc;
    if (isFloat) {
        tok.kind = TokenKind::FloatLiteral;
        tok.floatVal = std::strtof(text.c_str(), nullptr);
    } else {
        tok.kind = TokenKind::IntLiteral;
        tok.intVal = static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
    }
    return tok;
}

Token Lexer::nextToken() {
    skipWhitespaceAndComments();
    SourceLoc loc = currentLoc();

    if (pos_ >= source_.size()) {
        Token tok;
        tok.kind = TokenKind::Eof;
        tok.loc = loc;
        return tok;
    }

    char c = peek();

    if (std::isalpha(c) || c == '_') {
        return lexIdentifierOrKeyword(loc);
    }

    if (std::isdigit(c)) {
        return lexNumber(loc);
    }

    advance(); // Consume operator or punctuation
    Token tok;
    tok.loc = loc;
    tok.text = std::string(1, c);

    switch (c) {
        case '+':
            if (match('=')) { tok.kind = TokenKind::PlusEqual; tok.text = "+="; }
            else { tok.kind = TokenKind::Plus; }
            break;
        case '-':
            if (match('=')) { tok.kind = TokenKind::MinusEqual; tok.text = "-="; }
            else { tok.kind = TokenKind::Minus; }
            break;
        case '*':
            if (match('=')) { tok.kind = TokenKind::StarEqual; tok.text = "*="; }
            else { tok.kind = TokenKind::Star; }
            break;
        case '/':
            if (match('=')) { tok.kind = TokenKind::SlashEqual; tok.text = "/="; }
            else { tok.kind = TokenKind::Slash; }
            break;
        case '%':
            tok.kind = TokenKind::Percent;
            break;
        case '=':
            if (match('=')) { tok.kind = TokenKind::EqualEqual; tok.text = "=="; }
            else { tok.kind = TokenKind::Equal; }
            break;
        case '!':
            if (match('=')) { tok.kind = TokenKind::BangEqual; tok.text = "!="; }
            else { tok.kind = TokenKind::Bang; }
            break;
        case '<':
            if (match('=')) { tok.kind = TokenKind::LessEqual; tok.text = "<="; }
            else if (match('<')) { tok.kind = TokenKind::LessLess; tok.text = "<<"; }
            else { tok.kind = TokenKind::Less; }
            break;
        case '>':
            if (match('=')) { tok.kind = TokenKind::GreaterEqual; tok.text = ">="; }
            else if (match('>')) { tok.kind = TokenKind::GreaterGreater; tok.text = ">>"; }
            else { tok.kind = TokenKind::Greater; }
            break;
        case '&':
            if (match('&')) { tok.kind = TokenKind::AmpAmp; tok.text = "&&"; }
            else { tok.kind = TokenKind::Amp; }
            break;
        case '|':
            if (match('|')) { tok.kind = TokenKind::PipePipe; tok.text = "||"; }
            else { tok.kind = TokenKind::Pipe; }
            break;
        case '^':
            tok.kind = TokenKind::Caret;
            break;
        case '~':
            tok.kind = TokenKind::Tilde;
            break;
        case '.':
            tok.kind = TokenKind::Dot;
            break;
        case ',':
            tok.kind = TokenKind::Comma;
            break;
        case ';':
            tok.kind = TokenKind::Semicolon;
            break;
        case '(':
            tok.kind = TokenKind::LParen;
            break;
        case ')':
            tok.kind = TokenKind::RParen;
            break;
        case '{':
            tok.kind = TokenKind::LBrace;
            break;
        case '}':
            tok.kind = TokenKind::RBrace;
            break;
        case '[':
            tok.kind = TokenKind::LBracket;
            break;
        case ']':
            tok.kind = TokenKind::RBracket;
            break;
        default:
            tok.kind = TokenKind::Invalid;
            diags_.error(loc, "unexpected character '" + std::string(1, c) + "'");
            break;
    }

    return tok;
}

std::vector<Token> Lexer::tokenizeAll() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = nextToken();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof) break;
    }
    return tokens;
}

} // namespace kernelforge
