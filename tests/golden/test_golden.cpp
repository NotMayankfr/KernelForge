#include "doctest.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/TypeChecker.h"
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/SSABuilder.h"
#include "ir/IRPrinter.h"
#include "opt/PassManager.h"
#include "backend/InstructionSelector.h"
#include "backend/LinearScanRegAlloc.h"
#include "backend/AsmPrinter.h"
#include <fstream>
#include <iostream>

using namespace kernelforge;

TEST_CASE("Golden Test: vec_add.kl produces expected IR and assembly") {
    std::string klFile = "examples/vec_add.kl";
    std::ifstream in(klFile);
    if (!in.is_open()) {
        klFile = "../examples/vec_add.kl";
        in.open(klFile);
    }
    REQUIRE(in.is_open());
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    DiagnosticEngine diags(src);
    Lexer lexer(src, klFile, diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    REQUIRE(tc.check(ast.get()));

    ir::Module irMod("golden_vec_add");
    ir::IRBuilder builder(&irMod);
    builder.build(ast.get());

    for (const auto& func : irMod.functions) {
        ir::SSABuilder ssa;
        ssa.run(func.get());
    }

    auto pm = opt::PassManager::createO2();
    pm->run(&irMod);

    std::string irText = ir::IRPrinter::print(&irMod);
    CHECK(irText.find("kernel void @vec_add") != std::string::npos);
    CHECK(irText.find("get_special \"blockIdx.x\"") != std::string::npos);
    CHECK(irText.find("get_special \"threadIdx.x\"") != std::string::npos);
    CHECK(irText.find("icmp slt") != std::string::npos);
    CHECK(irText.find("fadd") != std::string::npos);

    backend::InstructionSelector isel;
    auto mMod = isel.select(&irMod);

    for (const auto& mFunc : mMod->functions) {
        backend::LinearScanRegAlloc regAlloc;
        regAlloc.allocate(mFunc.get());
    }

    std::string asmText = backend::AsmPrinter::print(mMod.get());
    CHECK(asmText.find(".entry vec_add") != std::string::npos);
    CHECK(asmText.find("ld.global.f32") != std::string::npos);
    CHECK(asmText.find("st.global.f32") != std::string::npos);
    CHECK(asmText.find("add.f32") != std::string::npos);
}

TEST_CASE("Golden Test: saxpy.kl produces expected IR and assembly") {
    std::string klFile = "examples/saxpy.kl";
    std::ifstream in(klFile);
    if (!in.is_open()) {
        klFile = "../examples/saxpy.kl";
        in.open(klFile);
    }
    REQUIRE(in.is_open());
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    DiagnosticEngine diags(src);
    Lexer lexer(src, klFile, diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    REQUIRE(tc.check(ast.get()));

    ir::Module irMod("golden_saxpy");
    ir::IRBuilder builder(&irMod);
    builder.build(ast.get());

    for (const auto& func : irMod.functions) {
        ir::SSABuilder ssa;
        ssa.run(func.get());
    }

    auto pm = opt::PassManager::createO2();
    pm->run(&irMod);

    std::string irText = ir::IRPrinter::print(&irMod);
    CHECK(irText.find("kernel void @saxpy") != std::string::npos);
    CHECK(irText.find("fmul") != std::string::npos);
    CHECK(irText.find("fadd") != std::string::npos);
}
