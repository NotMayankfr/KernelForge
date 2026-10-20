#pragma once

#include "asm/ObjectFormat.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace kernelforge {

class Linker {
public:
    struct LinkResult {
        bool success = false;
        std::vector<std::string> errors;
        ExecutableFile executable;
    };

    LinkResult link(const std::vector<ObjectFile>& objects);

    // Executable file I/O (.kx)
    static bool writeExecutable(const std::string& filename, const ExecutableFile& exe);
    static bool readExecutable(const std::string& filename, ExecutableFile& exe);

private:
    struct ResolvedSymbol {
        std::string name;
        uint32_t finalPC;
        SymbolType type;
        bool isKernel;
        uint16_t paramCount = 0;
    };
};

} // namespace kernelforge
