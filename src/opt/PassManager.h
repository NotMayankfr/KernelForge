#pragma once

#include "Pass.h"
#include "ir/Module.h"
#include <vector>
#include <memory>
#include <string>

namespace kernelforge::opt {

class PassManager {
public:
    PassManager(bool verifyAfterPass = true);

    void addPass(std::unique_ptr<Pass> pass);
    bool run(ir::Function* func);
    bool run(ir::Module* mod);

    static std::unique_ptr<PassManager> createO0();
    static std::unique_ptr<PassManager> createO1();
    static std::unique_ptr<PassManager> createO2();

    const std::vector<std::string>& getPassNames() const { return passNames_; }

private:
    bool verifyAfterPass_ = true;
    std::vector<std::unique_ptr<Pass>> passes_;
    std::vector<std::string> passNames_;
};

} // namespace kernelforge::opt
