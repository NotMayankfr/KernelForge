#include "Assembler.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace kernelforge {

uint8_t Assembler::encodeRegClass(backend::RegClass rc) {
    switch (rc) {
        case backend::RegClass::Int:     return 1;
        case backend::RegClass::Float:   return 2;
        case backend::RegClass::Pred:    return 3;
        case backend::RegClass::Special: return 4;
    }
    return 0;
}

uint8_t Assembler::encodeSpecialReg(const std::string& name) {
    if (name == "tid.x") return 0;
    if (name == "tid.y") return 1;
    if (name == "tid.z") return 2;
    if (name == "ctaid.x") return 3;
    if (name == "ctaid.y") return 4;
    if (name == "ctaid.z") return 5;
    if (name == "ntid.x") return 6;
    if (name == "ntid.y") return 7;
    if (name == "ntid.z") return 8;
    if (name == "nctaid.x") return 9;
    if (name == "nctaid.y") return 10;
    if (name == "nctaid.z") return 11;
    return 0;
}

std::string Assembler::decodeSpecialReg(uint8_t id) {
    switch (id) {
        case 0: return "tid.x";
        case 1: return "tid.y";
        case 2: return "tid.z";
        case 3: return "ctaid.x";
        case 4: return "ctaid.y";
        case 5: return "ctaid.z";
        case 6: return "ntid.x";
        case 7: return "ntid.y";
        case 8: return "ntid.z";
        case 9: return "nctaid.x";
        case 10: return "nctaid.y";
        case 11: return "nctaid.z";
    }
    return "special";
}

ObjectFile Assembler::assemble(const backend::MachineModule* mod) {
    ObjectFile obj;
    obj.strtab = "\0"; // initial null byte

    auto addString = [&obj](const std::string& str) -> uint32_t {
        uint32_t offset = static_cast<uint32_t>(obj.strtab.size());
        obj.strtab += str;
        obj.strtab.push_back('\0');
        return offset;
    };

    std::unordered_map<std::string, uint32_t> labelToPC;
    struct PendingReloc {
        uint32_t instIndex;
        std::string label;
        RelocType type;
    };
    std::vector<PendingReloc> pendingRelocs;

    // Process each function
    for (const auto& func : mod->functions) {
        uint32_t funcStartPC = static_cast<uint32_t>(obj.code.size());

        // Add function symbol
        SymbolEntry sym;
        sym.nameOffset = addString(func->name);
        sym.type = func->isKernel ? static_cast<uint8_t>(SymbolType::KERNEL) : static_cast<uint8_t>(SymbolType::FUNC);
        sym.binding = static_cast<uint8_t>(SymbolBinding::GLOBAL);
        sym.sectionIndex = 0; // .text section
        sym.value = funcStartPC; // instruction index
        sym.size = 0; // updated later

        uint32_t symIdx = static_cast<uint32_t>(obj.symbols.size());
        obj.symbols.push_back(sym);

        for (const auto& bb : func->blocks) {
            labelToPC[bb->name] = static_cast<uint32_t>(obj.code.size());

            for (const auto& mInst : bb->instructions) {
                BytecodeInst bInst;
                std::memset(&bInst, 0, sizeof(bInst));

                bInst.opcode = static_cast<uint8_t>(mInst->opcode);

                if (mInst->hasPredicate) {
                    bInst.flags |= 1; // bit 0
                    if (mInst->predInvert) bInst.flags |= 2; // bit 1
                    bInst.flags |= (mInst->predReg.id & 0x3F) << 2;
                }

                // Encode operands
                // Dst operand (index 0)
                if (!mInst->operands.empty()) {
                    const auto& op0 = mInst->operands[0];
                    if (op0.isReg()) {
                        bInst.dstClass = encodeRegClass(op0.reg.regClass);
                        if (op0.reg.regClass == backend::RegClass::Special) {
                            bInst.dstReg = encodeSpecialReg(op0.reg.specialName);
                        } else {
                            bInst.dstReg = static_cast<uint8_t>(op0.reg.id);
                        }
                    } else if (op0.isMem()) {
                        bInst.dstClass = 5; // Mem
                        bInst.dstReg = static_cast<uint8_t>(op0.reg.id);
                        bInst.immOrOffset = op0.memOffset;
                    } else if (op0.isLabel()) {
                        // Branch target or call target
                        pendingRelocs.push_back({static_cast<uint32_t>(obj.code.size()), op0.label,
                            mInst->opcode == backend::MachineOpcode::CALL ? RelocType::R_CALL_TARGET : RelocType::R_BRANCH_TARGET});
                    }
                }

                // Src1 operand (index 1)
                if (mInst->operands.size() > 1) {
                    const auto& op1 = mInst->operands[1];
                    if (op1.isReg()) {
                        bInst.src1Class = encodeRegClass(op1.reg.regClass);
                        if (op1.reg.regClass == backend::RegClass::Special) {
                            bInst.src1Reg = encodeSpecialReg(op1.reg.specialName);
                        } else {
                            bInst.src1Reg = static_cast<uint8_t>(op1.reg.id);
                        }
                    } else if (op1.isMem()) {
                        bInst.src1Class = 5; // Mem
                        bInst.src1Reg = static_cast<uint8_t>(op1.reg.id);
                        bInst.immOrOffset = op1.memOffset;
                    } else if (op1.kind == backend::OperandKind::ImmediateInt) {
                        bInst.src1Class = 6; // ImmInt
                        bInst.immOrOffset = op1.immInt;
                    } else if (op1.kind == backend::OperandKind::ImmediateFloat) {
                        bInst.src1Class = 7; // ImmFloat
                        std::memcpy(&bInst.immOrOffset, &op1.immFloat, 4);
                    } else if (op1.kind == backend::OperandKind::ImmediateBool) {
                        bInst.src1Class = 6; // ImmInt
                        bInst.immOrOffset = op1.immBool ? 1 : 0;
                    } else if (op1.isLabel()) {
                        pendingRelocs.push_back({static_cast<uint32_t>(obj.code.size()), op1.label, RelocType::R_BRANCH_TARGET});
                    }
                }

                // Src2 operand (index 2)
                if (mInst->operands.size() > 2) {
                    const auto& op2 = mInst->operands[2];
                    if (op2.isReg()) {
                        bInst.src2Class = encodeRegClass(op2.reg.regClass);
                        if (op2.reg.regClass == backend::RegClass::Special) {
                            bInst.src2Reg = encodeSpecialReg(op2.reg.specialName);
                        } else {
                            bInst.src2Reg = static_cast<uint8_t>(op2.reg.id);
                        }
                    } else if (op2.kind == backend::OperandKind::ImmediateInt) {
                        bInst.src2Class = 6; // ImmInt
                        bInst.immOrOffset = op2.immInt;
                    } else if (op2.kind == backend::OperandKind::ImmediateFloat) {
                        bInst.src2Class = 7; // ImmFloat
                        std::memcpy(&bInst.immOrOffset, &op2.immFloat, 4);
                    }
                }

                obj.code.push_back(bInst);
            }
        }

        uint32_t funcEndPC = static_cast<uint32_t>(obj.code.size());
        obj.symbols[symIdx].size = (funcEndPC - funcStartPC) * sizeof(BytecodeInst);
    }

    // Resolve local branch relocations or create relocation entries for cross-function symbols
    for (const auto& pr : pendingRelocs) {
        auto it = labelToPC.find(pr.label);
        if (it != labelToPC.end()) {
            // Local label resolved directly
            obj.code[pr.instIndex].immOrOffset = static_cast<int32_t>(it->second);
        } else {
            // Symbol in another object/function: emit relocation entry
            uint32_t symIdx = static_cast<uint32_t>(obj.symbols.size());
            SymbolEntry sym;
            sym.nameOffset = addString(pr.label);
            sym.type = static_cast<uint8_t>(SymbolType::FUNC);
            sym.binding = static_cast<uint8_t>(SymbolBinding::GLOBAL);
            sym.sectionIndex = 0;
            sym.value = 0;
            sym.size = 0;
            obj.symbols.push_back(sym);

            RelocEntry reloc;
            reloc.offset = pr.instIndex * static_cast<uint32_t>(sizeof(BytecodeInst));
            reloc.symbolIndex = symIdx;
            reloc.type = static_cast<uint32_t>(pr.type);
            reloc.addend = 0;
            obj.relocations.push_back(reloc);
        }
    }

    // Fill Header
    std::memcpy(obj.header.magic, KF_OBJ_MAGIC, 8);
    obj.header.version = KF_VERSION;
    obj.header.sectionCount = 4; // .text, .data, .symtab, .reloc
    obj.header.symbolCount = static_cast<uint32_t>(obj.symbols.size());
    obj.header.relocCount = static_cast<uint32_t>(obj.relocations.size());
    obj.header.entryOffset = 0;

    return obj;
}

bool Assembler::writeObjectFile(const std::string& filename, const ObjectFile& obj) {
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) return false;

    // 1. Header
    out.write(reinterpret_cast<const char*>(&obj.header), sizeof(ObjectHeader));

    // 2. Code section
    uint32_t codeBytes = static_cast<uint32_t>(obj.code.size() * sizeof(BytecodeInst));
    out.write(reinterpret_cast<const char*>(&codeBytes), sizeof(uint32_t));
    if (codeBytes > 0) {
        out.write(reinterpret_cast<const char*>(obj.code.data()), codeBytes);
    }

    // 3. Data section
    uint32_t dataBytes = static_cast<uint32_t>(obj.data.size());
    out.write(reinterpret_cast<const char*>(&dataBytes), sizeof(uint32_t));
    if (dataBytes > 0) {
        out.write(reinterpret_cast<const char*>(obj.data.data()), dataBytes);
    }

    // 4. Symbol table
    uint32_t symBytes = static_cast<uint32_t>(obj.symbols.size() * sizeof(SymbolEntry));
    out.write(reinterpret_cast<const char*>(&symBytes), sizeof(uint32_t));
    if (symBytes > 0) {
        out.write(reinterpret_cast<const char*>(obj.symbols.data()), symBytes);
    }

    // 5. String table
    uint32_t strBytes = static_cast<uint32_t>(obj.strtab.size());
    out.write(reinterpret_cast<const char*>(&strBytes), sizeof(uint32_t));
    if (strBytes > 0) {
        out.write(obj.strtab.data(), strBytes);
    }

    // 6. Relocations
    uint32_t relBytes = static_cast<uint32_t>(obj.relocations.size() * sizeof(RelocEntry));
    out.write(reinterpret_cast<const char*>(&relBytes), sizeof(uint32_t));
    if (relBytes > 0) {
        out.write(reinterpret_cast<const char*>(obj.relocations.data()), relBytes);
    }

    return out.good();
}

bool Assembler::readObjectFile(const std::string& filename, ObjectFile& obj) {
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) return false;

    // 1. Header
    in.read(reinterpret_cast<char*>(&obj.header), sizeof(ObjectHeader));
    if (std::memcmp(obj.header.magic, KF_OBJ_MAGIC, 8) != 0) {
        return false;
    }

    // 2. Code section
    uint32_t codeBytes = 0;
    in.read(reinterpret_cast<char*>(&codeBytes), sizeof(uint32_t));
    if (codeBytes > 0) {
        obj.code.resize(codeBytes / sizeof(BytecodeInst));
        in.read(reinterpret_cast<char*>(obj.code.data()), codeBytes);
    }

    // 3. Data section
    uint32_t dataBytes = 0;
    in.read(reinterpret_cast<char*>(&dataBytes), sizeof(uint32_t));
    if (dataBytes > 0) {
        obj.data.resize(dataBytes);
        in.read(reinterpret_cast<char*>(obj.data.data()), dataBytes);
    }

    // 4. Symbol table
    uint32_t symBytes = 0;
    in.read(reinterpret_cast<char*>(&symBytes), sizeof(uint32_t));
    if (symBytes > 0) {
        obj.symbols.resize(symBytes / sizeof(SymbolEntry));
        in.read(reinterpret_cast<char*>(obj.symbols.data()), symBytes);
    }

    // 5. String table
    uint32_t strBytes = 0;
    in.read(reinterpret_cast<char*>(&strBytes), sizeof(uint32_t));
    if (strBytes > 0) {
        obj.strtab.resize(strBytes);
        in.read(&obj.strtab[0], strBytes);
    }

    // 6. Relocations
    uint32_t relBytes = 0;
    in.read(reinterpret_cast<char*>(&relBytes), sizeof(uint32_t));
    if (relBytes > 0) {
        obj.relocations.resize(relBytes / sizeof(RelocEntry));
        in.read(reinterpret_cast<char*>(obj.relocations.data()), relBytes);
    }

    return in.good();
}

std::string Assembler::disassembleInst(const BytecodeInst& inst, uint32_t pc) {
    std::ostringstream ss;
    ss << "  [" << pc << "] ";

    if (inst.flags & 1) {
        bool inv = (inst.flags & 2) != 0;
        int predId = (inst.flags >> 2) & 0x3F;
        ss << "@" << (inv ? "!" : "") << "%P" << predId << " ";
    }

    ss << backend::machineOpcodeToString(static_cast<backend::MachineOpcode>(inst.opcode));

    auto printOperand = [&ss](uint8_t opClass, uint8_t opReg, int32_t imm) {
        switch (opClass) {
            case 1: ss << "%R" << static_cast<int>(opReg); break;
            case 2: ss << "%F" << static_cast<int>(opReg); break;
            case 3: ss << "%P" << static_cast<int>(opReg); break;
            case 4: ss << "%special" << static_cast<int>(opReg); break;
            case 5:
                if (imm == 0) ss << "[%R" << static_cast<int>(opReg) << "]";
                else ss << "[%R" << static_cast<int>(opReg) << " + " << imm << "]";
                break;
            case 6: ss << imm; break;
            case 7: {
                float f;
                std::memcpy(&f, &imm, 4);
                ss << f << "f";
                break;
            }
            default: break;
        }
    };

    if (inst.dstClass != 0) {
        ss << " ";
        printOperand(inst.dstClass, inst.dstReg, inst.immOrOffset);
    }

    if (inst.src1Class != 0) {
        ss << ", ";
        printOperand(inst.src1Class, inst.src1Reg, inst.immOrOffset);
    }

    if (inst.src2Class != 0) {
        ss << ", ";
        printOperand(inst.src2Class, inst.src2Reg, inst.immOrOffset);
    }

    if (inst.opcode == static_cast<uint8_t>(backend::MachineOpcode::BRA) ||
        inst.opcode == static_cast<uint8_t>(backend::MachineOpcode::CALL)) {
        ss << " target: " << inst.immOrOffset;
    }

    return ss.str();
}

std::string Assembler::disassemble(const ObjectFile& obj) {
    std::ostringstream ss;
    ss << "Object File: " << obj.code.size() << " instructions, "
       << obj.symbols.size() << " symbols, " << obj.relocations.size() << " relocations\n";

    for (size_t i = 0; i < obj.code.size(); ++i) {
        // Check if there is a symbol at this PC
        for (const auto& sym : obj.symbols) {
            if (sym.value == i) {
                ss << "\n" << obj.getSymbolName(sym.nameOffset) << ":\n";
            }
        }
        ss << disassembleInst(obj.code[i], static_cast<uint32_t>(i)) << "\n";
    }
    return ss.str();
}

} // namespace kernelforge
