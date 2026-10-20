#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

namespace kernelforge {

// Magic signatures
static constexpr char KF_OBJ_MAGIC[8] = {'\x7f', 'K', 'F', 'O', 'R', 'G', 'E', '\0'};
static constexpr char KF_EXE_MAGIC[8] = {'\x7f', 'K', 'F', '_', 'E', 'X', 'E', '\0'};
static constexpr uint32_t KF_VERSION = 1;

// Section types
enum class SectionType : uint32_t {
    PROGBITS = 1,
    SYMTAB   = 2,
    STRTAB   = 3,
    RELOC    = 4
};

// Symbol types & bindings
enum class SymbolType : uint8_t {
    KERNEL = 1,
    FUNC   = 2,
    OBJECT = 3
};

enum class SymbolBinding : uint8_t {
    LOCAL  = 0,
    GLOBAL = 1
};

// Relocation types
enum class RelocType : uint32_t {
    R_BRANCH_TARGET = 1,
    R_CALL_TARGET   = 2,
    R_DATA_ADDR     = 3
};

#pragma pack(push, 1)

struct ObjectHeader {
    char magic[8];
    uint32_t version;
    uint32_t sectionCount;
    uint32_t symbolCount;
    uint32_t relocCount;
    uint32_t entryOffset;
};

struct SectionHeader {
    char name[16];
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
};

struct SymbolEntry {
    uint32_t nameOffset;
    uint8_t type;
    uint8_t binding;
    uint16_t paramCount; // parameter count for KERNEL/FUNC symbols
    uint32_t value; // offset within section
    uint32_t size;
};

struct RelocEntry {
    uint32_t offset;       // byte offset within section to patch
    uint32_t symbolIndex;  // index in symbol table
    uint32_t type;         // RelocType
    int32_t addend;
};

// Fixed 16-byte binary machine instruction encoding
struct BytecodeInst {
    uint8_t opcode;        // MachineOpcode
    uint8_t flags;         // bit 0 = hasPredicate, bit 1 = predInvert, bits 2..4 = predRegId
    uint8_t dstClass;      // 0=None, 1=Int, 2=Float, 3=Pred, 4=Special, 5=Mem
    uint8_t dstReg;
    uint8_t src1Class;
    uint8_t src1Reg;
    uint8_t src2Class;
    uint8_t src2Reg;
    int32_t immOrOffset;   // 32-bit immediate integer, label address, or mem offset
    int32_t extra;         // float bits or extra payload
};

struct KernelDescriptor {
    char name[32];
    uint32_t entryPC;      // Instruction index in code
    uint32_t paramCount;
    uint32_t localStackSize;
    uint32_t sharedMemSize;
};

struct ExecutableHeader {
    char magic[8];
    uint32_t version;
    uint32_t kernelCount;
    uint32_t codeSize;     // in instructions (BytecodeInst)
    uint32_t dataSize;     // in bytes
};

#pragma pack(pop)

struct ObjectFile {
    ObjectHeader header;
    std::vector<SectionHeader> sections;
    std::vector<SymbolEntry> symbols;
    std::vector<RelocEntry> relocations;
    std::string strtab;
    std::vector<BytecodeInst> code;
    std::vector<uint8_t> data;

    std::string getSymbolName(uint32_t offset) const {
        if (offset >= strtab.size()) return "";
        return std::string(strtab.c_str() + offset);
    }
};

struct ExecutableFile {
    ExecutableHeader header;
    std::vector<KernelDescriptor> kernels;
    std::vector<BytecodeInst> code;
    std::vector<uint8_t> data;

    const KernelDescriptor* getKernel(const std::string& name) const {
        for (const auto& k : kernels) {
            if (std::string(k.name) == name) return &k;
        }
        return nullptr;
    }
};

} // namespace kernelforge
