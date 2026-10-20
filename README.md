# KernelForge

[![CI](https://github.com/kernelforge/kernelforge/actions/workflows/ci.yml/badge.svg)](https://github.com/kernelforge/kernelforge/actions/workflows/ci.yml)
[![Standard](https://img.shields.io/badge/c%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

**KernelForge** is a complete, self-contained compiler toolchain and virtual SIMT execution simulator for GPU-style compute kernels, written from scratch in modern C++17.

KernelForge translates a CUDA-like kernel language (`.kl`) through an SSA intermediate representation, runs an extensible optimization pipeline, performs instruction selection and linear-scan register allocation for a PTX-inspired virtual GPU ISA, assembles binary object files (`.ko`), links multi-file programs with relocations (`.kx`), and executes them on a cycle-level SIMT simulator featuring active masks, branch reconvergence stacks, and an interactive debugger.

---

## Architecture Pipeline

```
  vec_add.kl (Source)
        |
        v
  [ Frontend ] ---------> Hand-written Lexer, Recursive-descent Parser, Type Checker
        |
        v
  [ CFG Lowering ] -----> 3-Address Code Basic Blocks
        |
        v
  [ SSA Builder ] ------> Cooper-Harvey-Kennedy Dominator Tree, DF, phi-node insertion & renaming
        |
        v
  [ Optimizer ] --------> Constant Folding, DCE, CSE, Strength Reduction, LICM
        |
        v
  [ Backend ] ----------> Virtual ISA Selection, Linear-Scan Reg Alloc with Spilling
        |
        v
  [ Assembler (kas) ] --> Binary Object Format (.ko) with .text, .data, .symtab, .reloc
        |
        v
  [ Linker (kld) ] -----> Multi-object Symbol Resolution, Address Relocations -> Executable (.kx)
        |
        v
  [ Simulator (ksim) ] -> SIMT Warp Execution, Active Mask, Reconvergence Stack, Debugger
```

---

## Key Features

1. **Front End (`kfc/frontend`)**
   * Hand-written lexer and recursive-descent parser with precedence climbing for expressions.
   * Native GPU types: `int`, `float`, `bool`, and address space pointers (`global`, `shared`, `local`).
   * Hardware builtins: `threadIdx.{x,y,z}`, `blockIdx.{x,y,z}`, `blockDim.{x,y,z}`, `gridDim.{x,y,z}`.
   * Scoped symbol table and semantic type checking with line and column diagnostics.
   * Explicit thread-block execution barrier: `__sync()`.

2. **Intermediate Representation & SSA Form (`kfc/ir`)**
   * Strongly-typed 3-address control-flow graph (CFG).
   * **Cooper-Harvey-Kennedy Dominance Algorithm** calculating immediate dominators and dominance frontiers.
   * Automatic $\phi$-placement at iterated dominance frontiers ($IDF$) with scoped variable renaming.
   * Strict **IR Verifier** verifying dominator invariants (every use dominated by definition) after every pass.
   * Full IR printer and serializer.

3. **Optimizer (`kfc/opt`)**
   * Toggleable optimization pipeline (`PassManager` with `-O0`, `-O1`, `-O2`).
   * **Constant Folding** and **Algebraic Simplification** ($x \times 1 \to x$, $x + 0 \to x$, $x \oplus x \to 0$).
   * **Strength Reduction** (multiplication by powers-of-two converted to bitwise shifts).
   * **Common Subexpression Elimination (CSE)** and **Dead Code Elimination (DCE)**.
   * **Loop-Invariant Code Motion (LICM)** hoisting invariants out of loops into preheaders.

4. **Back End & Register Allocation (`kfc/backend`)**
   * PTX-inspired virtual GPU ISA with typed virtual registers, state spaces (`ld.global`, `st.shared`), predicated branching (`@%P0 bra`), and special registers.
   * Instruction selection from SSA IR.
   * **Linear-Scan Register Allocator** over live intervals with back-edge loop extensions and local-frame spilling (`[%R15 + offset]`).
   * Human-readable assembly generator (`.ka`).

5. **Assembler & Linker (`kas`, `kld`)**
   * Binary object format (`.ko`) containing magic headers, `.text`, `.data`, `.symtab`, and `.reloc`.
   * Multi-object linking with symbol resolution, undefined/duplicate symbol detection, and relocation patching.
   * Device-side runtime library (`runtime/runtime.kl`) providing device math functions (`kf_min`, `kf_max`, `kf_clamp`, etc.).

6. **SIMT Simulator & Interactive Debugger (`ksim`)**
   * SIMT warp execution: threads grouped into warps (8, 16, or 32 lanes) executing in lockstep.
   * Hardware active execution mask tracking live lanes.
   * **Branch Divergence Handling**: Reconvergence stack pushing post-dominator join points and popping them when divergent branches merge.
   * Global memory, per-block shared memory, and `__sync()` barrier support.
   * Full interactive CLI debugger: single-step (`step`, `next`), breakpoints (`break`, `delete`), register inspection (`info reg`), warp inspection (`info warps`), memory dumps (`mem`, `shared`), and disassembler (`disasm`).

---

## Directory Structure

```
KernelForge/
|-- CMakeLists.txt              # Top-level CMake configuration (-Wall -Wextra -pedantic)
|-- docs/
|   `-- design.md               # Detailed ISA spec, IR specification, and object format
|-- src/
|   |-- common/                 # Diagnostics, AST nodes, tokens
|   |-- frontend/               # Lexer, Parser, TypeChecker, SymbolTable
|   |-- ir/                     # BasicBlock, Function, Instruction, Dominators, SSA, Verifier
|   |-- opt/                    # ConstantFolding, DCE, CSE, StrengthReduction, LICM, PassManager
|   |-- backend/                # MachineIR, InstructionSelector, LivenessAnalysis, LinearScanRegAlloc
|   |-- asm/                    # Assembler, ObjectFile serialization & deserialization
|   |-- link/                   # Linker, Symbol resolution, Relocation patching, Executable format
|   |-- sim/                    # Simulator, SIMTState, Warps, ReconvergenceStack, Debugger
|   `-- tools/
|       |-- kfc.cpp             # Compiler driver
|       |-- kas.cpp             # Assembler driver & disassembler
|       |-- kld.cpp             # Linker driver
|       `-- ksim.cpp            # SIMT Simulator & Debugger
|-- runtime/
|   `-- runtime.kl              # Device runtime math library
|-- examples/
|   |-- vec_add.kl              # Vector addition kernel
|   |-- saxpy.kl                # SAXPY kernel
|   |-- reduce.kl               # Parallel tree reduction using shared memory & barriers
|   `-- divergence.kl           # SIMT branch divergence demonstration
`-- tests/
    |-- doctest.h               # Test framework
    |-- unit/                   # Unit tests for frontend, ir, opt, backend, asm, link, sim
    |-- golden/                 # Golden tests verifying IR and assembly output
    `-- differential/           # Differential testing (-O0 vs -O2 simulator parity)
```

---

## Building

### Requirements
* C++17 compliant compiler (`g++` >= 9 or `clang++` >= 10)
* `cmake` >= 3.15
* Linux, macOS, or Windows (WSL / MinGW)

### Build Instructions
```bash
git clone https://github.com/kernelforge/kernelforge.git
cd kernelforge
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

To run the test suite:
```bash
./kf_test
```

All 24 test cases will execute (frontend, SSA, optimization passes, backend, assembler, linker, golden tests, differential tests, and simulator execution).

---

## Quickstart & CLI Usage

### 1. Vector Addition Kernel (`examples/vec_add.kl`)
```c
kernel vec_add(global float* a, global float* b, global float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        c[i] = a[i] + b[i];
    }
}
```

### 2. Compile to Object File (`kfc`)
```bash
# Compile and optimize with -O2
./kfc ../examples/vec_add.kl -O2 -o vec_add.ko

# Inspect the generated SSA IR:
./kfc ../examples/vec_add.kl --emit-ir

# Inspect the generated virtual GPU assembly:
./kfc ../examples/vec_add.kl --emit-asm
```

### 3. Compile Device Runtime Library
```bash
./kfc ../runtime/runtime.kl -O2 -o runtime.ko
```

### 4. Link Objects into an Executable (`kld`)
```bash
./kld vec_add.ko runtime.ko -o vec_add.kx
```

### 5. Run on SIMT Virtual GPU Simulator (`ksim`)
```bash
./ksim vec_add.kx --grid 2 --block 16 --warp 8 --stats --dump-mem 0x3000 8
```

Output:
```
KernelForge Simulator launching 'vec_add' on Grid (2, 1, 1), Block (16, 1, 1), WarpSize 8 (32 total threads)
Kernel completed successfully.

=== Simulation Statistics ===
Total Instructions Executed: 92
Divergent Branches:          0
Global Memory Reads:         64
Global Memory Writes:        32
Barrier Syncs:               0

=== Dump Global Memory [0x3000 - 8 elements] ===
  [0] float=0 (int=0)
  [1] float=3 (int=1077936128)
  [2] float=6 (int=1086324736)
  [3] float=9 (int=1091567616)
  [4] float=12 (int=1094713344)
  [5] float=15 (int=1097859072)
  [6] float=18 (int=1099956224)
  [7] float=21 (int=1101529088)
```

---

## Interactive Debugger (`ksim --debug`)

Launch `ksim` with `--debug` to enter the interactive SIMT debugger:
```bash
./ksim vec_add.kx --debug
```

Interactive Debugger Session:
```
KernelForge Simulator launching 'vec_add' on Grid (1, 1, 1), Block (64, 1, 1), WarpSize 8 (64 total threads)
KernelForge Virtual GPU Interactive Debugger
Type 'help' for commands, 'step' to step, 'continue' to run.

=>  [0] mov.b32 %R0, %R0
    [1] mov.b32 %R1, %R1
    [2] mov.b32 %R2, %R2
(ksim) step
    [0] mov.b32 %R0, %R0
=>  [1] mov.b32 %R1, %R1
(ksim) info reg 0
--- Lane 0 (tid: 0) [ACTIVE] ---
  Int:   %R0=4096 %R1=8192 %R2=12288 %R3=64 %R4=0 %R5=0 %R6=0 %R7=0 
  Float: %F0=0f %F1=0f %F2=0f %F3=0f %F4=0f %F5=0f %F6=0f %F7=0f 
  Pred:  %P0=0 %P1=0 %P2=0 %P3=0 
(ksim) info warps
Total Blocks: 1
Block 0:
  Warp 0: PC=1 Mask=0xff (11111111) State=READY ReconvStackDepth=0
  Warp 1: PC=0 Mask=0xff (11111111) State=READY ReconvStackDepth=0
  ...
(ksim) continue
Kernel execution completed.
```

---

## Testing & Verification

KernelForge features a comprehensive test suite in `tests/`:
* **Unit Tests (`tests/unit/`):** Component tests for lexer, parser, type checker, IR lowering, SSA dominance calculations, each optimization pass, instruction selection, linear-scan allocator, assembler serialization, and multi-file linker.
* **Golden Tests (`tests/golden/`):** Verifies that source kernels (`vec_add.kl`, `saxpy.kl`) consistently produce expected IR and virtual assembly.
* **Differential Testing (`tests/differential/`):** Compiles and simulates identical kernels at both `-O0` and `-O2`, asserting bit-for-bit equivalence across all global memory outputs to catch optimizer regressions.

---

## License

KernelForge is licensed under the [MIT License](LICENSE).
