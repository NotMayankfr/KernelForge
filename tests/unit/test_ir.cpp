#include "doctest.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/TypeChecker.h"
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Dominators.h"
#include "ir/SSABuilder.h"
#include "ir/IRVerifier.h"
#include "ir/IRPrinter.h"
#include <iostream>

using namespace kernelforge;
using namespace kernelforge::ir;

TEST_CASE("IR lowering and SSA construction for vec_add") {
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

    Module mod("vec_add_module");
    IRBuilder builder(&mod);
    builder.build(ast.get());

    Function* func = mod.getFunction("vec_add");
    REQUIRE(func != nullptr);

    // Initial non-SSA verification
    std::string err;
    bool validNonSSA = IRVerifier::verify(func, &err);
    CHECK_MESSAGE(validNonSSA, err);

    // Run SSA Construction
    SSABuilder ssa;
    ssa.run(func);

    // Verify SSA Form
    bool validSSA = IRVerifier::verify(func, &err);
    CHECK_MESSAGE(validSSA, err);

    // Print SSA IR
    std::string irText = IRPrinter::print(func);
    CHECK(irText.find("kernel void @vec_add") != std::string::npos);
    CHECK(irText.find("get_special") != std::string::npos);
    CHECK(irText.find("icmp slt") != std::string::npos);
}

TEST_CASE("IR lowering with loops and SSA phi insertion") {
    std::string src = R"(
        kernel loop_test(global int* arr, int n) {
            int sum = 0;
            int i = 0;
            while (i < n) {
                sum += arr[i];
                i += 1;
            }
            arr[0] = sum;
        }
    )";
    DiagnosticEngine diags(src);
    Lexer lexer(src, "loop.kl", diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    REQUIRE(tc.check(ast.get()));

    Module mod("loop_module");
    IRBuilder builder(&mod);
    builder.build(ast.get());

    Function* func = mod.getFunction("loop_test");
    REQUIRE(func != nullptr);

    SSABuilder ssa;
    ssa.run(func);

    std::string err;
    bool ok = IRVerifier::verify(func, &err);
    CHECK_MESSAGE(ok, err);

    std::string irText = IRPrinter::print(func);
    // Should contain phi nodes for loop variables sum and i
    CHECK(irText.find("phi") != std::string::npos);
}

TEST_CASE("IRVerifier catches dominance violation") {
    Module mod("test_mod");
    Function* fn = mod.createFunction("bad_func", false, IRType::getVoid());
    BasicBlock* bb1 = fn->createBasicBlock("bb1");
    BasicBlock* bb2 = fn->createBasicBlock("bb2");

    bb1->addSuccessor(bb2);
    bb2->addPredecessor(bb1);

    // bb1 jumps to bb2
    auto jmp = std::make_unique<JumpInst>(bb2);
    bb1->addInstruction(std::move(jmp));

    // in bb2: create an add instruction %res = add 1, 2
    static ConstantInt one(1);
    static ConstantInt two(2);
    auto addInst = std::make_unique<BinaryInst>(Opcode::Add, &one, &two, "add1");
    Instruction* addPtr = addInst.get();
    bb2->addInstruction(std::move(addInst));
    bb2->addInstruction(std::make_unique<ReturnInst>(nullptr));

    // Intentionally violate SSA: insert a use of %add1 back in bb1 (before it's defined!)
    auto badUse = std::make_unique<BinaryInst>(Opcode::Add, addPtr, &one, "bad_use");
    bb1->insertInstructionAtBeginning(std::move(badUse));

    std::string err;
    bool valid = IRVerifier::verify(fn, &err);
    CHECK_FALSE(valid);
    CHECK(err.find("does not dominate use") != std::string::npos);
}
