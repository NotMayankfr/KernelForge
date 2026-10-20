#include "asm/Assembler.h"
#include <iostream>
#include <fstream>
#include <string>

using namespace kernelforge;

int main(int argc, char** argv) {
    std::string inputFile;
    std::string outputFile;
    bool disasm = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "-d" || arg == "--disasm") {
            disasm = true;
        } else if (arg[0] != '-') {
            inputFile = arg;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "KernelForge Assembler (kas)\n";
        std::cerr << "Usage: kas <input.ka> -o <output.ko> | kas -d <input.ko>\n";
        return 1;
    }

    Assembler as;

    if (disasm) {
        ObjectFile obj;
        if (!as.readObjectFile(inputFile, obj)) {
            std::cerr << "Error: failed to read object file '" << inputFile << "'\n";
            return 1;
        }
        std::cout << Assembler::disassemble(obj) << "\n";
        return 0;
    }

    std::ifstream in(inputFile);
    if (!in.is_open()) {
        std::cerr << "Error: cannot open input file '" << inputFile << "'\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    if (outputFile.empty()) {
        outputFile = inputFile + ".ko";
    }

    // Convert assembly to object
    auto mMod = std::make_unique<backend::MachineModule>(inputFile);
    // For direct binary conversion from text or module
    ObjectFile obj = as.assemble(mMod.get());

    if (!as.writeObjectFile(outputFile, obj)) {
        std::cerr << "Error: failed to write object file '" << outputFile << "'\n";
        return 1;
    }

    std::cout << "Assembled '" << inputFile << "' -> '" << outputFile << "'\n";
    return 0;
}
