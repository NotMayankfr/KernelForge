#pragma once

#include <cstdint>
#include <cstddef>

namespace kernelforge {

struct TargetInfo {
    static constexpr uint32_t DEFAULT_WARP_SIZE = 8;
    static constexpr uint32_t MAX_THREADS_PER_BLOCK = 1024;
    static constexpr uint32_t MAX_BLOCKS_PER_GRID = 65535;
    
    // Register allocation limits
    static constexpr uint32_t NUM_INT_REGS = 16;     // %r0 - %r15
    static constexpr uint32_t NUM_FLOAT_REGS = 16;   // %f0 - %f15
    static constexpr uint32_t NUM_PRED_REGS = 8;     // %p0 - %p7

    // Default physical registers for linear scan allocator
    static constexpr uint32_t ALLOCATABLE_INT_REGS = 8;    // %r0 - %r7
    static constexpr uint32_t ALLOCATABLE_FLOAT_REGS = 8;  // %f0 - %f7
    static constexpr uint32_t ALLOCATABLE_PRED_REGS = 4;   // %p0 - %p3

    // Memory sizes in simulator
    static constexpr size_t DEFAULT_GLOBAL_MEM_SIZE = 64 * 1024 * 1024; // 64 MB
    static constexpr size_t DEFAULT_SHARED_MEM_SIZE = 32 * 1024;        // 32 KB per block
    static constexpr size_t DEFAULT_LOCAL_MEM_SIZE  = 4 * 1024;         // 4 KB per thread
};

} // namespace kernelforge
