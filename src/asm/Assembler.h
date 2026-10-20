#pragma once

#include "ObjectFormat.h"
#include "backend/VirtualISA.h"
#include <string>
#include <vector>
#include <memory>

namespace kernelforge {

class Assembler {
public:
    // Assemble from backend MachineModule
    ObjectFile assemble(const backend::MachineModule* mod);

    // Assemble from assembly text (.ka)
    ObjectFile assembleText(const std::string& asmSource);

    // File I/O
    bool writeObjectFile(const std::string& filename, const ObjectFile& obj);
    bool readObjectFile(const std::string& filename, ObjectFile& obj);

    // Disassembler
    static std::string disassemble(const ObjectFile& obj);
    static std::string disassembleInst(const BytecodeInst& inst, uint32_t pc);

private:
    uint8_t encodeRegClass(backend::RegClass rc);
    uint8_t encodeSpecialReg(const std::string& name);
    std::string decodeSpecialReg(uint8_t id);
};

} // namespace kernelforge
