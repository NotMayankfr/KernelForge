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
#include <iostream>

using namespace kernelforge;
using namespace kernelforge::sim;

static ExecutableFile compileAndLink(const std::string& src, const std::string& name) {
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
    ObjectFile obj = as.assemble(mMod.get());

    Linker linker;
    auto res = linker.link({obj});
    return res.executable;
}

TEST_CASE("Simulator executes vec_add SIMT kernel") {
    std::string src = R"(
        kernel vec_add(global float* a, global float* b, global float* c, int n) {
            int i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i < n) {
                c[i] = a[i] + b[i];
            }
        }
    )";
    ExecutableFile exe = compileAndLink(src, "vec_add.kl");
    REQUIRE(!exe.kernels.empty());

    SimulationConfig config;
    config.gridDimX = 2;
    config.blockDimX = 16;
    config.warpSize = 8; // 2 warps per block

    Simulator sim(exe, config);

    uint32_t n = 32;
    uint32_t addrA = 0x1000;
    uint32_t addrB = 0x2000;
    uint32_t addrC = 0x3000;

    for (uint32_t i = 0; i < n; ++i) {
        sim.writeGlobalFloat(addrA + i * 4, static_cast<float>(i * 2));
        sim.writeGlobalFloat(addrB + i * 4, static_cast<float>(i * 3));
        sim.writeGlobalFloat(addrC + i * 4, 0.0f);
    }

    sim.initKernel("vec_add", {addrA, addrB, addrC, n});
    sim.run();

    // Verify results: c[i] == a[i] + b[i] == 5 * i
    for (uint32_t i = 0; i < n; ++i) {
        float expected = static_cast<float>(i * 5);
        float actual = sim.readGlobalFloat(addrC + i * 4);
        CHECK(actual == doctest::Approx(expected));
    }

    CHECK(sim.getStats().totalInstructions > 0);
}

TEST_CASE("Simulator handles branch divergence with reconvergence stack") {
    std::string src = R"(
        kernel divergence_test(global int* out, int n) {
            int tid = threadIdx.x;
            int val = 0;
            if (tid % 2 == 0) {
                val = 100;
            } else {
                val = 200;
            }
            out[tid] = val;
        }
    )";
    ExecutableFile exe = compileAndLink(src, "divergence.kl");
    REQUIRE(!exe.kernels.empty());

    SimulationConfig config;
    config.gridDimX = 1;
    config.blockDimX = 8;
    config.warpSize = 8;

    Simulator sim(exe, config);
    uint32_t addrOut = 0x1000;
    uint32_t n = 8;
    sim.initKernel("divergence_test", {addrOut, n});
    sim.run();

    // Verify divergence occurred
    CHECK(sim.getStats().divergentBranches >= 1);

    // Verify values: even threads got 100, odd threads got 200
    for (uint32_t i = 0; i < n; ++i) {
        int32_t val = static_cast<int32_t>(sim.readGlobalWord(addrOut + i * 4));
        if (i % 2 == 0) {
            CHECK(val == 100);
        } else {
            CHECK(val == 200);
        }
    }
}

TEST_CASE("Simulator executes parallel tree reduction with shared memory and barriers (reduce.kl)") {
    std::string src = R"(
        shared float sdata[256];

        kernel reduce_sum(global float* in, global float* out, int n) {
            int tid = threadIdx.x;
            int i = blockIdx.x * blockDim.x + threadIdx.x;

            if (i < n) {
                sdata[tid] = in[i];
            } else {
                sdata[tid] = 0.0f;
            }
            __sync();

            int s = blockDim.x / 2;
            while (s > 0) {
                if (tid < s) {
                    sdata[tid] += sdata[tid + s];
                }
                __sync();
                s = s / 2;
            }

            if (tid == 0) {
                out[blockIdx.x] = sdata[0];
            }
        }
    )";
    ExecutableFile exe = compileAndLink(src, "reduce.kl");
    REQUIRE(!exe.kernels.empty());

    SimulationConfig config;
    config.gridDimX = 1;
    config.blockDimX = 16;
    config.warpSize = 8; // 2 warps per block

    Simulator sim(exe, config);

    uint32_t n = 16;
    uint32_t addrIn = 0x1000;
    uint32_t addrOut = 0x2000;

    // in[i] = i for i=0..15 -> sum is 0+1+2+...+15 = 120
    for (uint32_t i = 0; i < n; ++i) {
        sim.writeGlobalFloat(addrIn + i * 4, static_cast<float>(i));
    }
    sim.writeGlobalFloat(addrOut, 0.0f);

    sim.initKernel("reduce_sum", {addrIn, addrOut, n});
    sim.run();

    // Verify barriers executed
    CHECK(sim.getStats().barrierCount >= 2);
    CHECK(sim.getStats().divergentBranches >= 1);

    // Verify reduction output: sum of 0..15 is 120.0f
    float totalSum = sim.readGlobalFloat(addrOut);
    CHECK(totalSum == doctest::Approx(120.0f));
}

