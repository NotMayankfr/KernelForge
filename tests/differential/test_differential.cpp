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
#include "asm/Assembler.h"
#include "link/Linker.h"
#include "sim/Simulator.h"
#include <random>
#include <iostream>

using namespace kernelforge;
using namespace kernelforge::sim;

static ExecutableFile compileWithOptLevel(const std::string& src, const std::string& optLevel) {
    DiagnosticEngine diags(src);
    Lexer lexer(src, "diff_test.kl", diags);
    auto tokens = lexer.tokenizeAll();
    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    TypeChecker tc(diags);
    tc.check(ast.get());

    ir::Module irMod("diff_mod");
    ir::IRBuilder builder(&irMod);
    builder.build(ast.get());

    for (const auto& func : irMod.functions) {
        ir::SSABuilder ssa;
        ssa.run(func.get());
    }

    std::unique_ptr<opt::PassManager> pm;
    if (optLevel == "-O0") pm = opt::PassManager::createO0();
    else pm = opt::PassManager::createO2();
    pm->run(&irMod);

    backend::InstructionSelector isel;
    auto mMod = isel.select(&irMod);

    for (const auto& mFunc : mMod->functions) {
        backend::LinearScanRegAlloc regAlloc;
        regAlloc.allocate(mFunc.get());
    }


    Assembler as;
    ObjectFile obj = as.assemble(mMod.get());

    Linker linker;
    auto res = linker.link({obj});
    return res.executable;
}

TEST_CASE("Differential Testing: -O0 vs -O2 produce bit-identical results on arithmetic & loops") {
    std::string src = R"(
        kernel diff_kernel(global int* out, int n) {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i < n) {
                int a = i * 4;       // Strength reduction candidate (i << 2)
                int b = a + 0;       // Algebraic simplification (+ 0)
                int c = a * 1;       // Algebraic simplification (* 1)
                int d = (i * 4) + b; // CSE candidate
                int sum = 0;
                for (int j = 0; j < 4; j += 1) {
                    sum += (d + j);
                }
                out[i] = sum + (c - c); // (c - c -> 0)
            }
        }
    )";

    ExecutableFile exeO0 = compileWithOptLevel(src, "-O0");
    ExecutableFile exeO2 = compileWithOptLevel(src, "-O2");

    REQUIRE(!exeO0.kernels.empty());
    REQUIRE(!exeO2.kernels.empty());

    // -O2 code should have fewer or equal instructions than -O0
    CHECK(exeO2.code.size() <= exeO0.code.size());

    SimulationConfig config;
    config.gridDimX = 2;
    config.blockDimX = 8;
    config.warpSize = 8;
    uint32_t n = 16;
    uint32_t outAddr = 0x1000;

    // Run at -O0
    Simulator simO0(exeO0, config);
    simO0.initKernel("diff_kernel", {outAddr, n});
    simO0.run();

    // Run at -O2
    Simulator simO2(exeO2, config);
    simO2.initKernel("diff_kernel", {outAddr, n});
    simO2.run();

    // Assert identical output memory
    for (uint32_t i = 0; i < n; ++i) {
        int32_t valO0 = static_cast<int32_t>(simO0.readGlobalWord(outAddr + i * 4));
        int32_t valO2 = static_cast<int32_t>(simO2.readGlobalWord(outAddr + i * 4));
        CHECK(valO0 == valO2);
    }
}

TEST_CASE("Differential Testing: -O0 vs -O2 with floating point calculations") {
    std::string src = R"(
        kernel diff_float(global float* out, int n) {
            int i = threadIdx.x;
            if (i < n) {
                float x = 2.5f * 2.0f; // Constant folding -> 5.0f
                float y = (x * 1.0f) + 0.0f; // Algebraic identities
                float z = y * 3.0f;
                out[i] = z + (x * 3.0f); // CSE opportunity
            }
        }
    )";

    ExecutableFile exeO0 = compileWithOptLevel(src, "-O0");
    ExecutableFile exeO2 = compileWithOptLevel(src, "-O2");

    SimulationConfig config;
    config.gridDimX = 1;
    config.blockDimX = 8;
    config.warpSize = 8;
    uint32_t n = 8;
    uint32_t outAddr = 0x2000;

    Simulator simO0(exeO0, config);
    simO0.initKernel("diff_float", {outAddr, n});
    simO0.run();

    Simulator simO2(exeO2, config);
    simO2.initKernel("diff_float", {outAddr, n});
    simO2.run();

    for (uint32_t i = 0; i < n; ++i) {
        float valO0 = simO0.readGlobalFloat(outAddr + i * 4);
        float valO2 = simO2.readGlobalFloat(outAddr + i * 4);
        CHECK(valO0 == doctest::Approx(valO2));
    }
}
