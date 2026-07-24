# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

`minc` is a minimal 8-bit CPU designed to run C on the smallest possible FPGA footprint (~400 LUT, see `Architecture_Comparison.md`). The repo contains three co-designed pieces:

- **`mincc/`** — a C compiler (subset of C) that emits `minc` assembly
- **`mincasm/`** — an assembler that turns that assembly into hex machine code
- **`verilog/`** — the CPU RTL, testbench, and memory models that execute the hex

Because the ISA, compiler, and hardware are developed together, changing the instruction set touches all three: `mincc/codegen.c` (what asm it emits), `mincasm/main.c` (`g_inst_specs` table — mnemonic → encoding), and `verilog/minc_h.sv` (decode logic), plus `Hardware.md` (instruction table docs).

## Build and test commands

```sh
make            # builds target/mincc.exe and target/mincasm.exe from mincc/ and mincasm/
make clean      # removes target/ and *.o
make test       # clean + build + runs tests/test.py (full pipeline test)
python3 tests/test.py   # run tests directly (requires target/mincc and target/mincasm already built)
```

`tests/test.py` (via `tests/testfuncs.py`) drives the full toolchain per case: `mincc` (C → asm) → `mincasm` (asm → hex) → write `verilog/test.hex` → `iverilog`/`vvp` simulate a core (default `verilog/minc_h.sv`) + `verilog/minc_tb.sv` (compiled with `-DTEST -DVERBOSE -DSIM`) → assert the resulting TOP-of-stack/PORTA/SP register values. This requires `iverilog` and `vvp` (Icarus Verilog) on PATH in addition to gcc. There is no test runner for isolated unit tests — every case is an end-to-end compile+assemble+simulate round trip; add new cases as entries in the `E2E_CASES` dict in `tests/test.py` (tuples of `code, expected_top, kwargs` consumed by `tf.test_e2e`), or as standalone `tf.expect(...)` / `tf.expect_fail(...)` calls for assembler/compiler error cases.

`tf.test_e2e` / `tf.test_irq` in `tests/testfuncs.py` accept a `core:str = "minc_h.sv"` argument so a case can be pointed at a different core file, but `minc_h.sv` is currently the only CPU implementation in the tree — there's no second core to compare against yet.

To manually run the pipeline on one program:

```sh
./target/mincc < example/demo.c > out.asm
./target/mincasm < out.asm > verilog/program.hex
cd verilog && iverilog -o sim.out minc_h.sv minc_tb.sv -g2012 -DVERBOSE -DSIM && vvp sim.out
```

## Architecture

### Toolchain (mincc)

Classic recursive-descent compiler, split into stages: `parse.c` (tokenizer), `ast.c` (parser — grammar documented at the top of `ast.h`), `codegen.c` (code generation), `nodes.c` (AST node / symbol table helpers), `errorhandle.c` (diagnostics). `mincc` reads C source from stdin and writes `minc` assembly to stdout; it always prepends a small crt0 (`mvi r14,0` / `mvi r15,0` / `calr __on_entry` / `calr main` / push results / `halt`).

Calling convention (see comment in `codegen.h`): arguments are passed in `r2..`; `r0`–`r5` are caller-saved, `r6`+ callee-saved; expression codegen treats `r2` upward as an implicit operand stack.

Supported C subset (see `ast.h` grammar comment): `int`/`char`/`uint8_t` (char/uint8_t/int are all currently 1 byte — see `PTR_SIZE`/type handling in `nodes.h`), pointers (`*`, `&`), `if`/`else`, `for`, `while`, `break`, function calls/recursion, global and local variables, and a `[[address=N]]` attribute for memory-mapped I/O variables (see `example/echoback.c`, `example/uart.c`).

### Assembler (mincasm)

Single-pass, table-driven (`mincasm/main.c`). `g_inst_specs[]` maps each mnemonic to an `InstKind` (e.g. `INST_ALU_RR`, `INST_MVI`, `INST_REL16`, `INST_MEM_STORE`/`LOAD`) describing how operands are encoded. Labels are resolved via a backpatch pass: forward/unresolved references are recorded as `Fixup` entries and patched in a second loop after the whole input is read (supports both forward and backward label references, `jz`/`calr`/`jr` relative offsets). Output is one 5-hex-digit word per instruction line (18-bit instruction width), fed directly to Verilog's `$readmemh`.

### CPU (verilog)

**`verilog/minc_h.sv` is the CPU implementation** (18-bit instruction word, 4-state `S_FETCH → S_DECEXEC → S_MA → S_WB` pipeline, memory-mapped SP at addresses `0x0000`/`0x0001`). `verilog/minc.sv` is an older/legacy core (15-bit instruction word, different opcode map and state machine) — check which one a task actually targets before editing; `minc_h.sv` is the one that gets active development.

`minc_h.sv` also has single-level interrupt hardware (see `Hardware.md`'s "割り込み" section): 4 level-triggered, fixed-priority request lines (`irq_in[3:0]`, `irq_in[0]` highest), a `PSR` (carry + interrupt-enable, memory-mapped at `0x0002`) and a one-level auto-saved `PSR_SHADOW` (`0x0003`) that the `reti` instruction (identical to `ret` plus restoring `PSR` from the shadow) restores. See `tests/fixtures/irq_vector.asm` + `tf.test_irq` in `tests/testfuncs.py` for the interrupt test harness (hand-assembled, bypasses `mincc` since there's no ORG/vector-table support in the toolchain).

Instruction decode centers on `op6`/`op4`/`op2` (top bits of the instruction) — see the `is_*` wires in `minc_h.sv` for the opcode map, and `Hardware.md` for the mnemonic/encoding table. 16 general-purpose 8-bit registers (`r0`–`r15`); `r12`–`r15` double as pointer/index register pairs (X = r12:r13, Y = r14:r15) for `stm`/`ldm` addressing.

`verilog/srom.sv` / `ssram.sv` are simple synthesizable ROM/RAM models used for simulation; `minc_tb.sv` also memory-maps a "port A" GPIO at addresses `0x0004`–`0x0006` for I/O testing.

### FPGA target (gowin/)

`gowin/minc/` is a Gowin IDE project (for an actual FPGA build/synthesis) wrapping the core in `gowin/minc/src/minc_gw_top.sv`, which adds a Gowin block-RAM instance and an optional UART (`uart_master/`). `UART`, `PORTA`, and `WAIT` (wait-state support) are toggled via `` `define `` in `minc_gw_top.sv` — enabling `UART` auto-enables `WAIT` since the UART core needs wait-state support to throttle CPU memory access.

## Notes

- `target/`, `mincc/*.o`, `mincasm/*.o`, and `verilog/*.vcd`/`*.exe` are build artifacts.
- `example/*.hex` and `verilog/program.hex` are pre-built hex outputs checked in for convenience/reference — regenerate them via the pipeline above if the corresponding `.c`/`.asm` source changes.
- `temp/` holds ad hoc synthesis/simulation scratch work (alu experiments, tcl scripts) — not part of the maintained build.
