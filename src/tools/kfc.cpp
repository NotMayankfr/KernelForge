#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/TypeChecker.h"
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/SSABuilder.h"
#include "ir/IRPrinter.h"
#include "ir/IRVerifier.h"
#include "opt/PassManager.h"
#include "backend/InstructionSelector.h"
#include "backend/LinearScanRegAlloc.h"
#include "backend/AsmPrinter.h"
#include "asm/Assembler.h"
#include <iostream>
#include <fstream>
#include <string>

using namespace kernelforge;

int main(int argc, char** argv) {
    std::string inputFile;
    std::string outputFile;
    std::string optLevel = "-O2";
    bool emitIR = false;
    bool emitASM = false;
    bool compileOnly = false; // -S

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "-O0" || arg == "-O1" || arg == "-O2") {
            optLevel = arg;
        } else if (arg == "-S") {
            compileOnly = true;
        } else if (arg == "--emit-ir") {
            emitIR = true;
        } else if (arg == "--emit-asm") {
            emitASM = true;
        } else if (arg[0] != '-') {
            inputFile = arg;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "KernelForge Compiler (kfc)\n";
        std::cerr << "Usage: kfc <input.kl> [-O0|-O1|-O2] [-S] [-o <output.ko>] [--emit-ir] [--emit-asm]\n";
        return 1;
    }

    std::ifstream in(inputFile);
    if (!in.is_open()) {
        std::cerr << "Error: cannot open input file '" << inputFile << "'\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    DiagnosticEngine diags(source);
    Lexer lexer(source, inputFile, diags);
    auto tokens = lexer.tokenizeAll();
    if (diags.hasErrors()) {
        diags.printAll();
        return 1;
    }

    Parser parser(tokens, diags);
    auto ast = parser.parseProgram();
    if (diags.hasErrors() || !ast) {
        diags.printAll();
        return 1;
    }

    TypeChecker tc(diags);
    if (!tc.check(ast.get())) {
        diags.printAll();
        return 1;
    }

    // Lower AST to IR
    ir::Module irMod(inputFile);
    ir::IRBuilder builder(&irMod);
    builder.build(ast.get());

    // Build SSA form
    for (const auto& func : irMod.functions) {
        ir::SSABuilder ssa;
        ssa.run(func.get());
    }

    // Run Optimizer
    std::unique_ptr<opt::PassManager> pm;
    if (optLevel == "-O0") pm = opt::PassManager::createO0();
    else if (optLevel == "-O1") pm = opt::PassManager::createO1();
    else pm = opt::PassManager::createO2();

    pm->run(&irMod);

    if (emitIR) {
        std::cout << ir::IRPrinter::print(&irMod) << "\n";
    }

    // Instruction Selection & Register Allocation
    backend::InstructionSelector isel;
    auto mMod = isel.select(&irMod);

    for (const auto& mFunc : mMod->functions) {
        backend::LinearScanRegAlloc regAlloc;
        regAlloc.allocate(mFunc.get());
    }

    if (emitASM || compileOnly) {
        std::string asmText = backend::AsmPrinter::print(mMod.get());
        if (compileOnly) {
            if (outputFile.empty()) outputFile = inputFile + ".ka";
            std::ofstream out(outputFile);
            out << asmText;
            std::cout << "Emitted assembly to '" << outputFile << "'\n";
            return 0;
        } else {
            std::cout << asmText << "\n";
        }
    }

    // Assemble to binary object format (.ko)
    Assembler assembler;
    ObjectFile obj = assembler.assemble(mMod.get());

    if (outputFile.empty()) {
        outputFile = inputFile + ".ko";
    }

    if (!assembler.writeObjectFile(outputFile, obj)) {
        std::cerr << "Error writing object file '" << outputFile << "'\n";
        return 1;
    }

    std::cout << "Compiled '" << inputFile << "' -> '" << outputFile << "' ("
              << obj.code.size() << " instructions, " << obj.symbols.size() << " symbols)\n";
    return 0;
}
