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
#include "asm/Assembler.h"
#include "link/Linker.h"
#include <iostream>

using namespace kernelforge;

static ObjectFile compileSourceToObject(const std::string& src, const std::string& name) {
    DiagnosticEngine diags(src);
    Lexer lexer(src, name, diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    tc.check(ast.get());

    ir::Module irMod(name);
    ir::IRBuilder builder(&irMod);
    builder.build(ast.get());

    for (const auto& func : irMod.functions) {
        ir::SSABuilder ssa;
        ssa.run(func.get());
    }

    auto pm = opt::PassManager::createO2();
    pm->run(&irMod);

    backend::InstructionSelector isel;
    auto mMod = isel.select(&irMod);

    for (const auto& mFunc : mMod->functions) {
        backend::LinearScanRegAlloc regAlloc;
        regAlloc.allocate(mFunc.get());
    }

    Assembler as;
    return as.assemble(mMod.get());
}

TEST_CASE("Assembler serializes and deserializes binary object") {
    std::string src = R"(
        kernel simple(global float* a, int n) {
            int tid = threadIdx.x;
            if (tid < n) {
                a[tid] = a[tid] + 1.0f;
            }
        }
    )";
    ObjectFile obj = compileSourceToObject(src, "simple.kl");
    REQUIRE(obj.code.size() > 0);
    REQUIRE(obj.symbols.size() > 0);

    Assembler as;
    std::string testFile = "/tmp/test_simple.ko";
    bool written = as.writeObjectFile(testFile, obj);
    CHECK(written);

    ObjectFile loaded;
    bool read = as.readObjectFile(testFile, loaded);
    CHECK(read);
    CHECK(loaded.code.size() == obj.code.size());
    CHECK(loaded.symbols.size() == obj.symbols.size());
}

TEST_CASE("Linker links multiple objects with cross-file function calls") {
    std::string runtimeSrc = R"(
        int kf_clamp(int x, int low, int high) {
            if (x < low) return low;
            if (x > high) return high;
            return x;
        }
    )";
    ObjectFile runtimeObj = compileSourceToObject(runtimeSrc, "runtime.kl");

    std::string kernelSrc = R"(
        int kf_clamp(int x, int low, int high);

        kernel clamp_kernel(global int* arr, int n) {
            int i = threadIdx.x;
            if (i < n) {
                arr[i] = kf_clamp(arr[i], 0, 100);
            }
        }
    )";
    ObjectFile kernelObj = compileSourceToObject(kernelSrc, "kernel.kl");

    Linker linker;
    auto res = linker.link({kernelObj, runtimeObj});
    CHECK(res.success);
    CHECK(res.errors.empty());
    CHECK(res.executable.code.size() == (kernelObj.code.size() + runtimeObj.code.size()));
    CHECK(res.executable.kernels.size() == 1);
    CHECK(std::string(res.executable.kernels[0].name) == "clamp_kernel");
}

TEST_CASE("Linker detects duplicate symbol error") {
    std::string src1 = "int helper() { return 1; }";
    std::string src2 = "int helper() { return 2; }";

    ObjectFile obj1 = compileSourceToObject(src1, "obj1.kl");
    ObjectFile obj2 = compileSourceToObject(src2, "obj2.kl");

    Linker linker;
    auto res = linker.link({obj1, obj2});
    CHECK_FALSE(res.success);
    CHECK(res.errors.size() >= 1);
    CHECK(res.errors[0].find("duplicate symbol") != std::string::npos);
}

TEST_CASE("Linker detects undefined symbol error") {
    std::string src = R"(
        int nonexistent_func(int x);

        kernel test_undef(global int* arr) {
            arr[0] = nonexistent_func(42);
        }
    )";
    ObjectFile obj = compileSourceToObject(src, "undef.kl");

    Linker linker;
    auto res = linker.link({obj});
    CHECK_FALSE(res.success);
    CHECK(res.errors.size() >= 1);
    CHECK(res.errors[0].find("undefined symbol") != std::string::npos);
}
