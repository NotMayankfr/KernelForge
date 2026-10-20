#pragma once

#include "SourceLoc.h"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>

namespace kernelforge {

enum class DiagLevel {
    Note,
    Warning,
    Error
};

struct Diagnostic {
    DiagLevel level;
    SourceLoc loc;
    std::string message;
};

class DiagnosticEngine {
public:
    DiagnosticEngine(std::string source = "") : sourceCode(std::move(source)) {}

    void setSource(std::string source) {
        sourceCode = std::move(source);
        splitLines();
    }

    void report(DiagLevel level, const SourceLoc& loc, const std::string& msg) {
        diagnostics.push_back({level, loc, msg});
        if (level == DiagLevel::Error) {
            errorCount_++;
        } else if (level == DiagLevel::Warning) {
            warningCount_++;
        }
    }

    void error(const SourceLoc& loc, const std::string& msg) {
        report(DiagLevel::Error, loc, msg);
    }

    void warning(const SourceLoc& loc, const std::string& msg) {
        report(DiagLevel::Warning, loc, msg);
    }

    void note(const SourceLoc& loc, const std::string& msg) {
        report(DiagLevel::Note, loc, msg);
    }

    bool hasErrors() const { return errorCount_ > 0; }
    size_t errorCount() const { return errorCount_; }
    size_t warningCount() const { return warningCount_; }
    const std::vector<Diagnostic>& getDiagnostics() const { return diagnostics; }

    void printAll(std::ostream& os = std::cerr) const {
        for (const auto& diag : diagnostics) {
            printDiagnostic(diag, os);
        }
    }

    std::string formatAll() const {
        std::ostringstream ss;
        printAll(ss);
        return ss.str();
    }

    void clear() {
        diagnostics.clear();
        errorCount_ = 0;
        warningCount_ = 0;
    }

private:
    std::string sourceCode;
    std::vector<std::string> lines;
    std::vector<Diagnostic> diagnostics;
    size_t errorCount_ = 0;
    size_t warningCount_ = 0;

    void splitLines() {
        lines.clear();
        std::istringstream stream(sourceCode);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
    }

    void printDiagnostic(const Diagnostic& diag, std::ostream& os) const {
        const char* levelStr = "error";
        if (diag.level == DiagLevel::Warning) levelStr = "warning";
        else if (diag.level == DiagLevel::Note) levelStr = "note";

        os << diag.loc.toString() << ": " << levelStr << ": " << diag.message << "\n";

        if (diag.loc.line > 0 && static_cast<size_t>(diag.loc.line) <= lines.size()) {
            const std::string& srcLine = lines[diag.loc.line - 1];
            os << "  " << srcLine << "\n";
            os << "  ";
            for (int i = 1; i < diag.loc.column; ++i) {
                if (i <= static_cast<int>(srcLine.size()) && srcLine[i - 1] == '\t') {
                    os << '\t';
                } else {
                    os << ' ';
                }
            }
            os << "^\n";
        }
    }
};

} // namespace kernelforge
