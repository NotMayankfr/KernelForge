#include "link/Linker.h"
#include "asm/Assembler.h"
#include <iostream>
#include <vector>
#include <string>

using namespace kernelforge;

int main(int argc, char** argv) {
    std::vector<std::string> inputFiles;
    std::string outputFile = "a.kx";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg[0] != '-') {
            inputFiles.push_back(arg);
        }
    }

    if (inputFiles.empty()) {
        std::cerr << "KernelForge Linker (kld)\n";
        std::cerr << "Usage: kld <file1.ko> <file2.ko> ... -o <a.kx>\n";
        return 1;
    }

    Assembler as;
    std::vector<ObjectFile> objects;

    for (const auto& file : inputFiles) {
        ObjectFile obj;
        if (!as.readObjectFile(file, obj)) {
            std::cerr << "Error: failed to read object file '" << file << "'\n";
            return 1;
        }
        objects.push_back(std::move(obj));
    }

    Linker linker;
    auto result = linker.link(objects);

    if (!result.success) {
        std::cerr << "Link error:\n";
        for (const auto& err : result.errors) {
            std::cerr << "  " << err << "\n";
        }
        return 1;
    }

    if (!Linker::writeExecutable(outputFile, result.executable)) {
        std::cerr << "Error writing executable file '" << outputFile << "'\n";
        return 1;
    }

    std::cout << "Linked " << inputFiles.size() << " object(s) -> '" << outputFile
              << "' (" << result.executable.code.size() << " instructions, "
              << result.executable.kernels.size() << " kernel(s))\n";
    return 0;
}
