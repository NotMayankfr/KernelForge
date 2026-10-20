#pragma once

#include <string>
#include <iostream>

namespace kernelforge {

struct SourceLoc {
    std::string filename;
    int line = 1;
    int column = 1;

    SourceLoc() = default;
    SourceLoc(std::string fn, int l, int c)
        : filename(std::move(fn)), line(l), column(c) {}

    std::string toString() const {
        if (filename.empty()) {
            return std::to_string(line) + ":" + std::to_string(column);
        }
        return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
};

} // namespace kernelforge
