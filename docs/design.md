# KernelForge Architecture & Design Specification

KernelForge is an end-to-end GPU-style compiler toolchain and virtual SIMT execution environment written in modern C++17. KernelForge implements every layer of the GPU compilation stack: from source language lexing, parsing, and type checking to SSA intermediate representation construction, optimization passes, instruction selection, register allocation, object assembly, multi-file linking, and warp-scheduled SIMT simulation with a cycle-accurate reconvergence stack and interactive debugger.

---

## 1. System Architecture Overview

```
                      +-----------------------------+
                      |   KernelForge Source (.kl)  |
                      +-----------------------------+
                                     |
                                     v
                       [ Frontend: Lexer + Parser ]
                                     |
                                     v
                           [ AST + Type Checker ]
                                     |
                                     v
                          [ IR & CFG Lowering ]
                                     |
                                     v
                       [ SSA Builder (Cooper-Harvey) ]
                                     |
                                     v
                        [ PassManager / Optimizer ]
                        |  - Constant Folding     |
                        |  - Algebraic Simpl.     |
                        |  - Dead Code Elim (DCE) |
                        |  - CSE                  |
                        |  - Strength Reduction   |
                        |  - LICM                 |
                        |  - IR Verifier          |
                                     |
                                     v
                        [ Instruction Selection ]
                                     |
                                     v
                      [ Linear-Scan Register Alloc ]
                                     |
                       +-------------+-------------+
                       |                           |
                       v                           v
              Assembly Text (.ka)         Object File (.ko)
                                                   |
                                                   v
                                         [ Linker (kld) ]
                                         (resolves symbols
                                          & relocations)
                                                   |
                                                   v
                                         Executable (.kx)
                                                   |
                                                   v
                                     [ SIMT Simulator (ksim) ]
                                     - Warp Lockstep (8/32 lanes)
                                     - Active Mask & Reconvergence
                                     - Global & Shared Memory
                                     - Interactive Debugger
```

---

## 2. Kernel Language Specification (.kl)

The KernelForge language (`.kl`) is a domain-specific C-like language designed for parallel compute kernels and device-side functions.

### 2.1 Types and Address Spaces
* **Primitive Types:** `int` (32-bit signed integer), `float` (32-bit IEEE 754 single-precision float), `bool` (1-bit boolean predicate), `void`.
* **Address Spaces:**
  * `global`: Pointer points to off-chip high-latency global memory shared across all warps and blocks (e.g., `global float* a`).
  * `shared`: Data resides in on-chip, low-latency scratchpad memory allocated per thread-block (e.g., `shared float sdata[256];`).
  * `local`: Thread-private variables and register-spilled data.
* **Builtin Constants:**
  * `threadIdx.x`, `threadIdx.y`, `threadIdx.z`: Dimension indices for current thread within its thread-block.
  * `blockIdx.x`, `blockIdx.y`, `blockIdx.z`: Dimension indices for the current thread-block within the grid.
  * `blockDim.x`, `blockDim.y`, `blockDim.z`: Thread-block dimensions.
  * `gridDim.x`, `gridDim.y`, `gridDim.z`: Execution grid dimensions.

### 2.2 Control Flow and Synchronization
* **Statements:** `if / else`, `while`, `for`, `return`, expression statements, variable declarations.
* **Barriers:** `__sync()` compiles to a warp synchronization barrier (`bar.sync 0`), stalling warps in a thread-block until all warps reach the barrier before resuming execution.

### 2.3 Example Kernel
```c
kernel vec_add(global float* a, global float* b, global float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        c[i] = a[i] + b[i];
    }
}
```

---

## 3. Intermediate Representation (IR) & SSA Construction

KernelForge implements a strongly-typed, three-address code intermediate representation structured as a Control Flow Graph (CFG) of basic blocks.

### 3.1 Three-Address Instructions
* **Arithmetic / Logic:** `add`, `sub`, `mul`, `sdiv`, `srem`, `shl`, `ashr`, `and`, `or`, `xor`, `fadd`, `fsub`, `fmul`, `fdiv`.
* **Comparisons:** `icmp` (`eq`, `ne`, `slt`, `sle`, `sgt`, `sge`), `fcmp` (`oeq`, `one`, `olt`, `ole`, `ogt`, `oge`).
* **Memory Operations:**
  * `gep` (GetElementPtr): Calculates typed address offsets: $\text{base} + \text{index} \times \text{element\_size}$.
  * `load`: Reads value from pointer with specified address space.
  * `store`: Writes value to pointer with specified address space.
* **Control Flow:**
  * `br <cond>, label %then, label %else`: Conditional branch.
  * `jmp label %target`: Unconditional jump.
  * `ret [val]`: Return from function or kernel.
* **Special Instructions:**
  * `phi [val1, %bb1], [val2, %bb2]`: SSA static single assignment phi node.
  * `get_special <name>`: Accesses hardware registers (`threadIdx.x`, etc.).
  * `sync`: Hardware execution barrier.
  * `call @fn(args...)`: Function call.

### 3.2 SSA Form Construction
1. **Dominator Tree Calculation:**
   * Uses the **Cooper-Harvey-Kennedy** algorithm for fast computation of immediate dominators ($idom$):
   $$\text{doms}(b) = \{b\} \cup \left(\bigcap_{p \in \text{preds}(b)} \text{doms}(p)\right)$$
2. **Dominance Frontiers:**
   * Identifies nodes where dominance ceases:
   $$DF(X) = \{ Y \mid \exists P \in \text{preds}(Y) : X \in \text{dom}(P) \land X \notin \text{strict\_dom}(Y) \}$$
3. **$\phi$-Placement:**
   * For every variable assigned across multiple basic blocks, $\phi$-nodes are placed at the iterated dominance frontier ($IDF$).
4. **Renaming:**
   * Scoped stack-based renaming recursively walks the dominator tree, assigning versioned SSA values and ensuring every use is dominated by its single definition.
5. **IR Verifier:**
   * Enforces SSA invariants:
     * Valid CFG successor/predecessor invariants.
     * All operands in $\phi$-nodes correspond to incoming CFG edges.
     * **Dominance Invariant:** Every operand use is strictly dominated by its defining instruction (or incoming block for $\phi$).

---

## 4. Optimization Pipeline

KernelForge includes a modular `PassManager` running optimization passes with verifier validation between each step:

| Pass | Description | Example Transformation |
| :--- | :--- | :--- |
| **Constant Folding** | Evaluates operations on compile-time constants | `add i32 4, 8` $\to$ `12` |
| **Algebraic Simplification** | Exploits algebraic identities | `x + 0` $\to$ `x`, `x * 1` $\to$ `x`, `x * 0` $\to$ `0`, `x ^ x` $\to$ `0` |
| **Strength Reduction** | Replaces costly operations with cheaper equivalents | `x * 2` $\to$ `x << 1`, `x * 8` $\to$ `x << 3` |
| **Dead Code Elimination (DCE)** | Recursively eliminates instructions with zero uses | Removals of dead computations and unused GEPs |
| **Common Subexpression Elim (CSE)** | Replaces duplicate expressions within blocks | `a = b + c; d = b + c` $\to$ `d = a` |
| **Loop-Invariant Code Motion (LICM)** | Identifies loops and hoists invariant expressions to the loop preheader | Hoists invariant pointer calculations outside loop bodies |

* Optimization levels:
  * `-O0`: Minimal passes, direct lowering.
  * `-O1`: Local constant folding, algebraic simplification, and dead code elimination.
  * `-O2`: Full pipeline including global CSE, strength reduction, and LICM.

---

## 5. Virtual GPU ISA Specification

KernelForge compiles to a PTX-inspired virtual instruction set designed for massively parallel execution.

### 5.1 Architecture & Register Model
* **Virtual Register Classes:**
  * 32-bit Integer Registers: `%R0`, `%R1`, `%R2`, ..., `%R7` (Allocatable); `%R12` (Scratch 1), `%R13` (Scratch 2), `%R14` (Dest Spill), `%R15` (Frame Base).
  * 32-bit Floating-Point Registers: `%F0`, `%F1`, ..., `%F7`.
  * 1-bit Predicate Registers: `%P0`, `%P1`, `%P2`, `%P3`.
* **Hardware Special Registers:**
  * `%tid.x`, `%tid.y`, `%tid.z`
  * `%ntid.x`, `%ntid.y`, `%ntid.z` (`blockDim`)
  * `%ctaid.x`, `%ctaid.y`, `%ctaid.z` (`blockIdx`)
  * `%nctaid.x`, `%nctaid.y`, `%nctaid.z` (`gridDim`)

### 5.2 Instruction Format
Instructions support predicated execution (`@%P0 inst` or `@!%P0 inst`), explicit state spaces, and typed operands:
* `mov.b32 %dst, %src`
* `add.s32 %dst, %src1, %src2`, `sub.s32`, `mul.s32`, `div.s32`, `rem.s32`, `shl.b32`, `shr.s32`
* `add.f32 %dst, %src1, %src2`, `sub.f32`, `mul.f32`, `div.f32`
* `setp.lt.s32 %p, %a, %b` (Predicated comparisons: `eq`, `ne`, `lt`, `le`, `gt`, `ge`)
* `ld.global.f32 %dst, [%src]`, `ld.shared.f32 %dst, [%src]`, `ld.local.f32 %dst, [%src]`
* `st.global.f32 [%dst], %src`, `st.shared.f32 [%dst], %src`, `st.local.f32 [%dst], %src`
* `bra <target>` (Branch), `@%P0 bra <target>` (Predicated branch)
* `bar.sync 0` (Thread-block barrier)
* `call <symbol>`, `ret`

### 5.3 Register Allocation: Linear Scan with Spilling
* **Liveness Analysis:** Computes live ranges $[start, end]$ for all virtual registers across basic blocks, accurately extending intervals across loop back-edges.
* **Linear Scan Allocation:** Sorts intervals by start point and assigns available physical registers from `%R0..%R7` or `%F0..%F7`.
* **Spill Handling:** When register pressure exceeds physical registers, intervals with the furthest end points are spilled to local frame memory (`[%R15 + offset]`), with transparent reload insertion using reserved scratch registers `%R12..%R14`.

---

## 6. Binary Object Format (.ko) & Executable Format (.kx)

### 6.1 Object File (.ko) Layout
An object file emitted by `kas` consists of:
```
+----------------------------------------------------+
| Header: Magic (0x464F5247 "FORG"), Version, Offsets|
+----------------------------------------------------+
| .text Section: Bytecode Instructions               |
+----------------------------------------------------+
| .data Section: Static global/constant storage      |
+----------------------------------------------------+
| .symtab Section: Symbols (Exported/Imported/Local) |
+----------------------------------------------------+
| .reloc Section: Relocations (PC-rel & Abs32 calls) |
+----------------------------------------------------+
```

### 6.2 Linker (kld) Operations
1. **Section Merging:** Merges `.text` and `.data` from multiple input object files.
2. **Symbol Resolution:** Maintains global symbol table; matches `IMPORT` references to `EXPORT` definitions. Detects duplicate definition errors and undefined symbol errors.
3. **Relocation Patching:**
   * Recalculates relative branch offsets and absolute function call targets based on consolidated section layouts.
4. **Executable Output (`.kx`):** Produces linked executable with kernel descriptor table for simulator launch.

---

## 7. SIMT Simulator & Debugger (ksim)

The `ksim` simulator models a SIMT (Single Instruction, Multiple Threads) architecture executing warps in lockstep.

```
       Block Execution
    +--------------------+
    | Warp 0 (Lanes 0..7)| ---> Executes in lockstep with Active Mask
    | Warp 1 (Lanes 0..7)|
    | Warp 2 (Lanes 0..7)|
    +--------------------+
         |
         | Branch Divergence (e.g. if (tid % 2 == 0))
         v
    Reconvergence Stack:
    +-----------------------------------------------+
    | Entry: ReconvPC, NextMask (Inactive Lanes)    |
    +-----------------------------------------------+
    Current Execution: Active Mask = 0x55 (Even Lanes)
         |
         | Execution reaches ReconvPC (post-dominator)
         v
    Pop Stack -> Active Mask restores to 0xFF (Reconverged!)
```

### 7.1 Warp Execution & Active Masks
* Threads within a block are organized into fixed-size warps (configurable, e.g., 8 or 32 threads).
* Each warp tracks an **Active Mask** (bitmask of active threads).
* Instructions only update state for lanes whose active bit is set.

### 7.2 Branch Divergence & Reconvergence Stack
* When a predicated branch causes warp lanes to take different paths (e.g., condition true for lanes 0..3, false for lanes 4..7):
  1. The warp pushes a `ReconvergenceEntry` onto its reconvergence stack containing:
     * `reconvergePC`: The post-dominator join address where the paths reunite.
     * `divergedMask`: The mask for lanes taking the alternative path.
  2. The warp continues along the taken path with mask = `takenMask`.
  3. Upon reaching `reconvergePC`, the stack entry is popped and the remaining lanes execute, or both branches reunite if both paths have completed.

### 7.3 Memory Hierarchy
* **Global Memory:** Flat 32-bit address space accessible by all warps across all blocks.
* **Shared Memory:** Block-local memory buffer accessible by all threads in the same block, synchronized via `bar.sync`.
* **Local Memory:** Thread-private memory for stack frames and register spilling.

### 7.4 Interactive CLI Debugger
The built-in debugger provides complete inspection of the SIMT execution state:
* `step` / `s`: Step single instruction for current warp.
* `next` / `n`: Step one instruction across all active warps.
* `continue` / `c`: Run until breakpoint or termination.
* `break` / `b <pc>`: Set breakpoint at PC.
* `info warps`: Display PC, active mask, and status of all warps.
* `info reg [lane]`: Inspect `%R0..%R7`, `%F0..%F7`, and `%P0..%P3` for a specific lane.
* `mask`: View active mask and reconvergence stack depth.
* `mem <addr> <count>`: Inspect global memory.
* `shared <offset> <count>`: Inspect thread-block shared memory.
* `disasm [pc]`: Disassemble instructions around current PC.
