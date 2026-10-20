cmake_minimum_required(VERSION 3.16)
project(KernelForge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -pedantic -Wno-unused-parameter)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Common & Frontend sources
set(FRONTEND_SOURCES
    src/frontend/Token.cpp
    src/frontend/Lexer.cpp
    src/frontend/Parser.cpp
    src/frontend/TypeChecker.cpp
)

# IR sources
set(IR_SOURCES
    src/ir/IR.cpp
    src/ir/Dominators.cpp
    src/ir/IRBuilder.cpp
    src/ir/SSABuilder.cpp
    src/ir/IRVerifier.cpp
    src/ir/IRPrinter.cpp
)

# Opt sources
set(OPT_SOURCES
    src/opt/PassManager.cpp
    src/opt/ConstantFolding.cpp
    src/opt/AlgebraicSimplification.cpp
    src/opt/DeadCodeElimination.cpp
    src/opt/CommonSubexpressionElimination.cpp
    src/opt/StrengthReduction.cpp
    src/opt/LoopInvariantCodeMotion.cpp
)

# Backend sources
set(BACKEND_SOURCES
    src/backend/VirtualISA.cpp
    src/backend/InstructionSelector.cpp
    src/backend/LivenessAnalysis.cpp
    src/backend/LinearScanRegAlloc.cpp
    src/backend/AsmPrinter.cpp
)

# Assembler sources
set(ASM_SOURCES
    src/asm/Assembler.cpp
)

# Linker sources
set(LINK_SOURCES
    src/link/Linker.cpp
)

# Simulator sources
set(SIM_SOURCES
    src/sim/Simulator.cpp
    src/sim/Debugger.cpp
)

add_library(kf_frontend STATIC ${FRONTEND_SOURCES})
add_library(kf_ir STATIC ${IR_SOURCES})
target_link_libraries(kf_ir PUBLIC kf_frontend)

add_library(kf_opt STATIC ${OPT_SOURCES})
target_link_libraries(kf_opt PUBLIC kf_ir)

add_library(kf_backend STATIC ${BACKEND_SOURCES})
target_link_libraries(kf_backend PUBLIC kf_ir)

add_library(kf_asm STATIC ${ASM_SOURCES})
target_link_libraries(kf_asm PUBLIC kf_backend)

add_library(kf_link STATIC ${LINK_SOURCES})
target_link_libraries(kf_link PUBLIC kf_asm)

add_library(kf_sim STATIC ${SIM_SOURCES})
target_link_libraries(kf_sim PUBLIC kf_link)

# CLI Executables
add_executable(kfc src/tools/kfc.cpp)
target_link_libraries(kfc PRIVATE kf_frontend kf_ir kf_opt kf_backend kf_asm)

add_executable(kas src/tools/kas.cpp)
target_link_libraries(kas PRIVATE kf_asm)

add_executable(kld src/tools/kld.cpp)
target_link_libraries(kld PRIVATE kf_link)

add_executable(ksim src/tools/ksim.cpp)
target_link_libraries(ksim PRIVATE kf_sim)

# Test executable for doctest
file(GLOB UNIT_TEST_SOURCES tests/unit/*.cpp)
file(GLOB GOLDEN_TEST_SOURCES tests/golden/*.cpp)
file(GLOB DIFF_TEST_SOURCES tests/differential/*.cpp)

set(ALL_TEST_SOURCES ${UNIT_TEST_SOURCES} ${GOLDEN_TEST_SOURCES} ${DIFF_TEST_SOURCES})

if(ALL_TEST_SOURCES)
    add_executable(kf_test ${ALL_TEST_SOURCES})
    target_link_libraries(kf_test PRIVATE
        kf_frontend
        kf_ir
        kf_opt
        kf_backend
        kf_asm
        kf_link
        kf_sim
    )
endif()
