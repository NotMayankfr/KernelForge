#include "VirtualISA.h"
#include <sstream>

namespace kernelforge::backend {

const char* machineOpcodeToString(MachineOpcode op) {
    switch (op) {
        case MachineOpcode::ADD_S32: return "add.s32";
        case MachineOpcode::SUB_S32: return "sub.s32";
        case MachineOpcode::MUL_S32: return "mul.s32";
        case MachineOpcode::DIV_S32: return "div.s32";
        case MachineOpcode::REM_S32: return "rem.s32";
        case MachineOpcode::SHL_B32: return "shl.b32";
        case MachineOpcode::SHR_S32: return "shr.s32";
        case MachineOpcode::AND_B32: return "and.b32";
        case MachineOpcode::OR_B32:  return "or.b32";
        case MachineOpcode::XOR_B32: return "xor.b32";

        case MachineOpcode::ADD_F32: return "add.f32";
        case MachineOpcode::SUB_F32: return "sub.f32";
        case MachineOpcode::MUL_F32: return "mul.f32";
        case MachineOpcode::DIV_F32: return "div.f32";
        case MachineOpcode::NEG_F32: return "neg.f32";

        case MachineOpcode::SETP_EQ_S32: return "setp.eq.s32";
        case MachineOpcode::SETP_NE_S32: return "setp.ne.s32";
        case MachineOpcode::SETP_LT_S32: return "setp.lt.s32";
        case MachineOpcode::SETP_LE_S32: return "setp.le.s32";
        case MachineOpcode::SETP_GT_S32: return "setp.gt.s32";
        case MachineOpcode::SETP_GE_S32: return "setp.ge.s32";

        case MachineOpcode::SETP_EQ_F32: return "setp.eq.f32";
        case MachineOpcode::SETP_NE_F32: return "setp.ne.f32";
        case MachineOpcode::SETP_LT_F32: return "setp.lt.f32";
        case MachineOpcode::SETP_LE_F32: return "setp.le.f32";
        case MachineOpcode::SETP_GT_F32: return "setp.gt.f32";
        case MachineOpcode::SETP_GE_F32: return "setp.ge.f32";

        case MachineOpcode::MOV_B32: return "mov.b32";
        case MachineOpcode::MOV_F32: return "mov.f32";
        case MachineOpcode::CVT_F32_S32: return "cvt.f32.s32";
        case MachineOpcode::CVT_S32_F32: return "cvt.s32.f32";

        case MachineOpcode::LD_GLOBAL_S32: return "ld.global.s32";
        case MachineOpcode::LD_GLOBAL_F32: return "ld.global.f32";
        case MachineOpcode::ST_GLOBAL_S32: return "st.global.s32";
        case MachineOpcode::ST_GLOBAL_F32: return "st.global.f32";

        case MachineOpcode::LD_SHARED_S32: return "ld.shared.s32";
        case MachineOpcode::LD_SHARED_F32: return "ld.shared.f32";
        case MachineOpcode::ST_SHARED_S32: return "st.shared.s32";
        case MachineOpcode::ST_SHARED_F32: return "st.shared.f32";

        case MachineOpcode::LD_LOCAL_S32:  return "ld.local.s32";
        case MachineOpcode::LD_LOCAL_F32:  return "ld.local.f32";
        case MachineOpcode::ST_LOCAL_S32:  return "st.local.s32";
        case MachineOpcode::ST_LOCAL_F32:  return "st.local.f32";

        case MachineOpcode::LD_PARAM_S32:  return "ld.param.s32";
        case MachineOpcode::LD_PARAM_F32:  return "ld.param.f32";

        case MachineOpcode::BRA: return "bra";
        case MachineOpcode::CALL: return "call";
        case MachineOpcode::RET: return "ret";
        case MachineOpcode::EXIT: return "exit";
        case MachineOpcode::BAR_SYNC: return "bar.sync";

        case MachineOpcode::LABEL: return ".label";
    }
    return "unknown";
}

std::string MachineInst::toString() const {
    std::ostringstream ss;
    if (opcode == MachineOpcode::LABEL) {
        if (!operands.empty()) {
            ss << "$" << operands[0].label << ":";
        }
        return ss.str();
    }

    ss << "    ";
    if (hasPredicate) {
        ss << "@" << (predInvert ? "!" : "") << predReg.toString() << " ";
    }

    ss << machineOpcodeToString(opcode);

    for (size_t i = 0; i < operands.size(); ++i) {
        if (i == 0) ss << " ";
        else ss << ", ";
        ss << operands[i].toString();
    }

    ss << ";";
    return ss.str();
}

} // namespace kernelforge::backend
