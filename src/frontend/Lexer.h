#pragma once

#include "Token.h"
#include "common/Diagnostic.h"
#include <string>
#include <vector>

namespace kernelforge {

class Lexer {
public:
    Lexer(std::string source, std::string filename, DiagnosticEngine& diags);

    Token nextToken();
    std::vector<Token> tokenizeAll();

    const std::string& getSource() const { return source_; }
    const std::string& getFilename() const { return filename_; }

private:
    std::string source_;
    std::string filename_;
    DiagnosticEngine& diags_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);
    void skipWhitespaceAndComments();
    SourceLoc currentLoc() const;

    Token lexIdentifierOrKeyword(const SourceLoc& startLoc);
    Token lexNumber(const SourceLoc& startLoc);
};

} // namespace kernelforge
