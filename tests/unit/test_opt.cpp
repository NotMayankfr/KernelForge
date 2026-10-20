#include "doctest.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/TypeChecker.h"
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/SSABuilder.h"
#include "ir/IRPrinter.h"
#include "ir/IRVerifier.h"
#include "opt/PassManager.h"
#include "opt/ConstantFolding.h"
#include "opt/AlgebraicSimplification.h"
#include "opt/DeadCodeElimination.h"
#include "opt/CommonSubexpressionElimination.h"
#include "opt/StrengthReduction.h"
#include "opt/LoopInvariantCodeMotion.h"
#include <iostream>

using namespace kernelforge;
using namespace kernelforge::ir;
using namespace kernelforge::opt;

static std::unique_ptr<Function> compileToSSA(const std::string& src, const std::string& fnName) {
    DiagnosticEngine diags(src);
    Lexer lexer(src, "test.kl", diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    if (!tc.check(ast.get())) return nullptr;

    auto mod = std::make_unique<Module>("test_mod");
    IRBuilder builder(mod.get());
    builder.build(ast.get());

    Function* func = mod->getFunction(fnName);
    if (!func) return nullptr;

    SSABuilder ssa;
    ssa.run(func);

    // Detach from module so test owns it
    for (auto it = mod->functions.begin(); it != mod->functions.end(); ++it) {
        if (it->get() == func) {
            auto res = std::move(*it);
            mod->functions.erase(it);
            return res;
        }
    }
    return nullptr;
}

TEST_CASE("Opt: ConstantFolding folds arithmetic") {
    std::string src = R"(
        kernel test_cf(global int* out) {
            int a = 10 + 20;
            int b = a * 2;
            out[0] = b;
        }
    )";
    auto func = compileToSSA(src, "test_cf");
    REQUIRE(func != nullptr);

    ConstantFoldingPass cf;
    bool changed = cf.runOnFunction(func.get());
    CHECK(changed);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));

    std::string ir = IRPrinter::print(func.get());
    // Should have folded 10 + 20 to 30, and 30 * 2 to 60
    CHECK(ir.find("60") != std::string::npos);
}

TEST_CASE("Opt: AlgebraicSimplification simplifies identities") {
    std::string src = R"(
        kernel test_alg(global int* out, int x) {
            int y = x + 0;
            int z = y * 1;
            int w = z - z;
            out[0] = w;
        }
    )";
    auto func = compileToSSA(src, "test_alg");
    REQUIRE(func != nullptr);

    AlgebraicSimplificationPass alg;
    bool changed = alg.runOnFunction(func.get());
    CHECK(changed);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));

    std::string ir = IRPrinter::print(func.get());
    // z - z should become 0
    CHECK(ir.find("0") != std::string::npos);
}

TEST_CASE("Opt: DeadCodeElimination removes unused instructions") {
    std::string src = R"(
        kernel test_dce(global int* out, int x) {
            int unused1 = x * 100;
            int unused2 = unused1 + 50;
            out[0] = x;
        }
    )";
    auto func = compileToSSA(src, "test_dce");
    REQUIRE(func != nullptr);

    size_t beforeCount = func->getInstructionCount();
    DeadCodeEliminationPass dce;
    bool changed = dce.runOnFunction(func.get());
    CHECK(changed);

    size_t afterCount = func->getInstructionCount();
    CHECK(afterCount < beforeCount);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));

    size_t mulCount = 0;
    for (const auto& bb : func->basicBlocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->opcode == Opcode::Mul) mulCount++;
        }
    }
    CHECK(mulCount == 0);
}

TEST_CASE("Opt: CommonSubexpressionElimination reuses identical expressions") {
    std::string src = R"(
        kernel test_cse(global int* out, int a, int b) {
            int x = a * b;
            int y = a * b;
            out[0] = x + y;
        }
    )";
    auto func = compileToSSA(src, "test_cse");
    REQUIRE(func != nullptr);

    CommonSubexpressionEliminationPass cse;
    bool changed = cse.runOnFunction(func.get());
    CHECK(changed);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));

    size_t mulCount = 0;
    for (const auto& bb : func->basicBlocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->opcode == Opcode::Mul) mulCount++;
        }
    }
    CHECK(mulCount == 1);
}

TEST_CASE("Opt: StrengthReduction transforms mul to shift") {
    std::string src = R"(
        kernel test_sr(global int* out, int x) {
            int y = x * 8;
            int z = x + x;
            out[0] = y + z;
        }
    )";
    auto func = compileToSSA(src, "test_sr");
    REQUIRE(func != nullptr);

    StrengthReductionPass sr;
    bool changed = sr.runOnFunction(func.get());
    CHECK(changed);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));

    size_t mulCount = 0;
    size_t shlCount = 0;
    for (const auto& bb : func->basicBlocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->opcode == Opcode::Mul) mulCount++;
            if (inst->opcode == Opcode::Shl) shlCount++;
        }
    }
    CHECK(mulCount == 0);
    CHECK(shlCount >= 2); // both x*8 and x+x converted to shl
}

TEST_CASE("Opt: LoopInvariantCodeMotion hoists invariant outside loop") {
    std::string src = R"(
        kernel test_licm(global int* arr, int n, int scale) {
            int i = 0;
            int c = 42 * scale; // invariant
            while (i < n) {
                int inv = scale * 5;
                arr[i] = arr[i] + inv;
                i += 1;
            }
        }
    )";
    auto func = compileToSSA(src, "test_licm");
    REQUIRE(func != nullptr);

    LoopInvariantCodeMotionPass licm;
    bool changed = licm.runOnFunction(func.get());
    CHECK(changed);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));
}

TEST_CASE("Opt: Full -O2 pipeline executes with automatic verification") {
    std::string src = R"(
        kernel full_opt(global float* a, global float* b, global float* c, int n) {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i < n) {
                float valA = a[i];
                float valB = b[i];
                float sum = (valA * 1.0f) + (valB + 0.0f);
                c[i] = sum;
            }
        }
    )";
    auto func = compileToSSA(src, "full_opt");
    REQUIRE(func != nullptr);

    auto pm = PassManager::createO2();
    bool changed = pm->run(func.get());
    CHECK(changed);

    std::string err;
    CHECK(IRVerifier::verify(func.get(), &err));
}
