#include "AsmPrinter.h"
#include <sstream>

namespace kernelforge::backend {

std::string AsmPrinter::print(const MachineInst* inst) {
    if (!inst) return "";
    return inst->toString();
}

std::string AsmPrinter::print(const MachineBasicBlock* bb) {
    if (!bb) return "";
    std::ostringstream ss;
    ss << "$" << bb->name << ":\n";
    for (const auto& inst : bb->instructions) {
        ss << print(inst.get()) << "\n";
    }
    return ss.str();
}

std::string AsmPrinter::print(const MachineFunction* func) {
    if (!func) return "";
    std::ostringstream ss;

    if (func->isKernel) ss << ".entry " << func->name << " (\n";
    else ss << ".func " << func->name << " (\n";

    for (size_t i = 0; i < func->paramNames.size(); ++i) {
        ss << "    .param .b32 " << func->paramNames[i];
        if (i + 1 < func->paramNames.size()) ss << ",\n";
        else ss << "\n";
    }
    ss << ") {\n";

    ss << "    .reg .s32 %R<8>;\n";
    ss << "    .reg .f32 %F<8>;\n";
    ss << "    .reg .pred %P<4>;\n";

    if (func->localStackSize > 0) {
        ss << "    .local .b32 __local_stack[" << func->localStackSize << "];\n";
    }

    ss << "\n";

    for (const auto& bb : func->blocks) {
        ss << print(bb.get());
    }

    ss << "}\n";
    return ss.str();
}

std::string AsmPrinter::print(const MachineModule* mod) {
    if (!mod) return "";
    std::ostringstream ss;
    ss << "// KernelForge Virtual GPU Assembly\n";
    ss << ".version 1.0\n";
    ss << ".target kfsim\n\n";

    for (const auto& func : mod->functions) {
        ss << print(func.get()) << "\n";
    }

    return ss.str();
}

} // namespace kernelforge::backend
