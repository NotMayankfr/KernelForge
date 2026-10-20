#include "doctest.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/TypeChecker.h"
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/SSABuilder.h"
#include "opt/PassManager.h"
#include "backend/InstructionSelector.h"
#include "backend/LinearScanRegAlloc.h"
#include "backend/AsmPrinter.h"
#include <iostream>

using namespace kernelforge;
using namespace kernelforge::ir;
using namespace kernelforge::backend;

TEST_CASE("Backend: Instruction Selection and Linear Scan Register Allocation for vec_add") {
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
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    REQUIRE(tc.check(ast.get()));

    Module irMod("vec_add_mod");
    IRBuilder builder(&irMod);
    builder.build(ast.get());

    Function* func = irMod.getFunction("vec_add");
    REQUIRE(func != nullptr);

    SSABuilder ssa;
    ssa.run(func);

    auto pm = opt::PassManager::createO2();
    pm->run(func);

    // Instruction Selection
    InstructionSelector isel;
    auto mMod = isel.select(&irMod);
    REQUIRE(mMod != nullptr);
    REQUIRE(mMod->functions.size() == 1);

    MachineFunction* mFunc = mMod->functions[0].get();
    REQUIRE(mFunc != nullptr);
    CHECK(mFunc->name == "vec_add");

    // Register Allocation
    LinearScanRegAlloc regAlloc(8, 8, 4);
    regAlloc.allocate(mFunc);

    // Print Assembly
    std::string asmText = AsmPrinter::print(mMod.get());
    CHECK(asmText.find(".entry vec_add") != std::string::npos);
    CHECK(asmText.find("ld.global.f32") != std::string::npos);
    CHECK(asmText.find("st.global.f32") != std::string::npos);
    CHECK(asmText.find("add.f32") != std::string::npos);
    CHECK(asmText.find("ret;") != std::string::npos);
}
