#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/TypeChecker.h"

using namespace kernelforge;

TEST_CASE("Lexer tokenizes vec_add kernel") {
    std::string src = R"(
        kernel vec_add(global float* a, global float* b, global float* c, int n) {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i < n) {
                c[i] = a[i] + b[i];
            }
        }
    )";
    DiagnosticEngine diags(src);
    Lexer lexer(src, "vec_add.kl", diags);
    auto tokens = lexer.tokenizeAll();

    CHECK_FALSE(diags.hasErrors());
    CHECK(tokens.size() > 10);
    CHECK(tokens[0].kind == TokenKind::KwKernel);
    CHECK(tokens[1].kind == TokenKind::Identifier);
    CHECK(tokens[1].text == "vec_add");
}

TEST_CASE("Parser parses and typechecks vec_add kernel") {
    std::string src = R"(
        kernel vec_add(global float* a, global float* b, global float* c, int n) {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i < n) {
                c[i] = a[i] + b[i];
            }
        }
    )";
    DiagnosticEngine diags(src);
    Lexer lexer(src, "vec_add.kl", diags);
    auto tokens = lexer.tokenizeAll();
    REQUIRE_FALSE(diags.hasErrors());

    Parser parser(tokens, diags);
    auto program = parser.parseProgram();
    REQUIRE_FALSE(diags.hasErrors());
    REQUIRE(program != nullptr);
    REQUIRE(program->functions.size() == 1);

    TypeChecker tc(diags);
    bool ok = tc.check(program.get());
    CHECK(ok);
    CHECK_FALSE(diags.hasErrors());
}

TEST_CASE("TypeChecker detects undeclared identifier") {
    std::string src = R"(
        kernel bad(global float* a) {
            int x = unknown_var + 1;
        }
    )";
    DiagnosticEngine diags(src);
    Lexer lexer(src, "bad.kl", diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto program = parser.parseProgram();

    TypeChecker tc(diags);
    bool ok = tc.check(program.get());
    CHECK_FALSE(ok);
    CHECK(diags.hasErrors());
}
