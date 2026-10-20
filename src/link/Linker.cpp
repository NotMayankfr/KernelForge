#include "Linker.h"
#include "backend/VirtualISA.h"
#include <fstream>
#include <cstring>
#include <iostream>

namespace kernelforge {

Linker::LinkResult Linker::link(const std::vector<ObjectFile>& objects) {
    LinkResult res;
    if (objects.empty()) {
        res.errors.push_back("no input object files provided");
        return res;
    }

    std::unordered_map<std::string, ResolvedSymbol> globalSymbols;
    std::vector<uint32_t> objectCodeBaseOffsets;
    uint32_t totalInstructions = 0;

    // 1. Pass 1: Layout sections & resolve defined symbols
    for (size_t objIdx = 0; objIdx < objects.size(); ++objIdx) {
        const ObjectFile& obj = objects[objIdx];
        objectCodeBaseOffsets.push_back(totalInstructions);

        for (const auto& sym : obj.symbols) {
            std::string name = obj.getSymbolName(sym.nameOffset);
            if (name.empty()) continue;

            // Check if symbol is defined in this object (non-zero size)
            if (sym.size > 0) {
                auto it = globalSymbols.find(name);
                if (it != globalSymbols.end()) {
                    res.errors.push_back("duplicate symbol definition: '" + name + "'");
                } else {
                    ResolvedSymbol rs;
                    rs.name = name;
                    rs.finalPC = totalInstructions + sym.value;
                    rs.type = static_cast<SymbolType>(sym.type);
                    rs.isKernel = (rs.type == SymbolType::KERNEL);
                    rs.paramCount = sym.paramCount;
                    globalSymbols[name] = rs;
                }
            }
        }

        totalInstructions += static_cast<uint32_t>(obj.code.size());
    }

    // 2. Pass 2: Check for undefined symbols in relocations
    for (size_t objIdx = 0; objIdx < objects.size(); ++objIdx) {
        const ObjectFile& obj = objects[objIdx];
        for (const auto& reloc : obj.relocations) {
            if (reloc.symbolIndex >= obj.symbols.size()) {
                res.errors.push_back("relocation symbol index out of bounds in object " + std::to_string(objIdx));
                continue;
            }
            std::string symName = obj.getSymbolName(obj.symbols[reloc.symbolIndex].nameOffset);
            if (globalSymbols.find(symName) == globalSymbols.end()) {
                res.errors.push_back("undefined symbol reference: '" + symName + "'");
            }
        }
    }

    if (!res.errors.empty()) {
        res.success = false;
        return res;
    }

    // 3. Pass 3: Merge code and apply relocations
    ExecutableFile& exe = res.executable;
    exe.code.reserve(totalInstructions);

    for (size_t objIdx = 0; objIdx < objects.size(); ++objIdx) {
        const ObjectFile& obj = objects[objIdx];
        uint32_t basePC = objectCodeBaseOffsets[objIdx];

        size_t startInExe = exe.code.size();
        exe.code.insert(exe.code.end(), obj.code.begin(), obj.code.end());

        // Adjust local branches in this object by basePC
        for (size_t instIdx = 0; instIdx < obj.code.size(); ++instIdx) {
            auto& bInst = exe.code[startInExe + instIdx];
            if (bInst.opcode == static_cast<uint8_t>(backend::MachineOpcode::BRA) &&
                bInst.dstClass == 0 && bInst.src1Class == 0) {
                // Local branch offset adjustment
                // If it wasn't a relocation entry, it was a local branch
                bool isRelocated = false;
                for (const auto& r : obj.relocations) {
                    if (r.offset == instIdx * sizeof(BytecodeInst)) {
                        isRelocated = true;
                        break;
                    }
                }
                if (!isRelocated) {
                    bInst.immOrOffset += basePC;
                }
            }
        }

        // Apply external relocations
        for (const auto& reloc : obj.relocations) {
            uint32_t instIdx = reloc.offset / static_cast<uint32_t>(sizeof(BytecodeInst));
            uint32_t patchTargetPC = basePC + instIdx;

            std::string symName = obj.getSymbolName(obj.symbols[reloc.symbolIndex].nameOffset);
            const auto& rs = globalSymbols[symName];

            uint32_t targetPC = rs.finalPC + reloc.addend;
            exe.code[patchTargetPC].immOrOffset = static_cast<int32_t>(targetPC);
        }
    }

    // 4. Build Kernel Descriptor Table
    for (const auto& [name, sym] : globalSymbols) {
        if (sym.isKernel) {
            KernelDescriptor kd;
            std::memset(&kd, 0, sizeof(kd));
            std::strncpy(kd.name, name.c_str(), sizeof(kd.name) - 1);
            kd.entryPC = sym.finalPC;
            kd.paramCount = sym.paramCount > 0 ? sym.paramCount : 4;
            kd.localStackSize = 1024;
            kd.sharedMemSize = 16384;
            exe.kernels.push_back(kd);
        }
    }

    // Fill executable header
    std::memcpy(exe.header.magic, KF_EXE_MAGIC, 8);
    exe.header.version = KF_VERSION;
    exe.header.kernelCount = static_cast<uint32_t>(exe.kernels.size());
    exe.header.codeSize = static_cast<uint32_t>(exe.code.size());
    exe.header.dataSize = static_cast<uint32_t>(exe.data.size());

    res.success = true;
    return res;
}

bool Linker::writeExecutable(const std::string& filename, const ExecutableFile& exe) {
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) return false;

    // Header
    out.write(reinterpret_cast<const char*>(&exe.header), sizeof(ExecutableHeader));

    // Kernel descriptors
    uint32_t kBytes = static_cast<uint32_t>(exe.kernels.size() * sizeof(KernelDescriptor));
    out.write(reinterpret_cast<const char*>(&kBytes), sizeof(uint32_t));
    if (kBytes > 0) {
        out.write(reinterpret_cast<const char*>(exe.kernels.data()), kBytes);
    }

    // Code
    uint32_t codeBytes = static_cast<uint32_t>(exe.code.size() * sizeof(BytecodeInst));
    out.write(reinterpret_cast<const char*>(&codeBytes), sizeof(uint32_t));
    if (codeBytes > 0) {
        out.write(reinterpret_cast<const char*>(exe.code.data()), codeBytes);
    }

    // Data
    uint32_t dataBytes = static_cast<uint32_t>(exe.data.size());
    out.write(reinterpret_cast<const char*>(&dataBytes), sizeof(uint32_t));
    if (dataBytes > 0) {
        out.write(reinterpret_cast<const char*>(exe.data.data()), dataBytes);
    }

    return out.good();
}

bool Linker::readExecutable(const std::string& filename, ExecutableFile& exe) {
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) return false;

    // Header
    in.read(reinterpret_cast<char*>(&exe.header), sizeof(ExecutableHeader));
    if (std::memcmp(exe.header.magic, KF_EXE_MAGIC, 8) != 0) {
        return false;
    }

    // Kernel descriptors
    uint32_t kBytes = 0;
    in.read(reinterpret_cast<char*>(&kBytes), sizeof(uint32_t));
    if (kBytes > 0) {
        exe.kernels.resize(kBytes / sizeof(KernelDescriptor));
        in.read(reinterpret_cast<char*>(exe.kernels.data()), kBytes);
    }

    // Code
    uint32_t codeBytes = 0;
    in.read(reinterpret_cast<char*>(&codeBytes), sizeof(uint32_t));
    if (codeBytes > 0) {
        exe.code.resize(codeBytes / sizeof(BytecodeInst));
        in.read(reinterpret_cast<char*>(exe.code.data()), codeBytes);
    }

    // Data
    uint32_t dataBytes = 0;
    in.read(reinterpret_cast<char*>(&dataBytes), sizeof(uint32_t));
    if (dataBytes > 0) {
        exe.data.resize(dataBytes);
        in.read(reinterpret_cast<char*>(exe.data.data()), dataBytes);
    }

    return in.good();
}

} // namespace kernelforge
