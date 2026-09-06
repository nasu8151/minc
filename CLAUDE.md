# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

`minc` is a minimal 8-bit CPU designed to run C on the smallest possible FPGA footprint (~400 LUT, see `Architecture_Comparison.md`). The repo contains three co-designed pieces:

- **`mincc/`** — a C compiler (subset of C) that emits `minc` assembly
- **`mincasm/`** — an assembler that turns that assembly into hex machine code
- **`verilog/`** — the CPU RTL, testbench, and memory models that execute the hex
- **`temp/`** - if you need temporary files, you **MUST** use this directory.

Because the ISA, compiler, and hardware are developed together, changing the instruction set touches all three: `mincc/codegen.c` (what asm it emits), `mincasm/main.c` (`g_inst_specs` table — mnemonic → encoding), and `verilog/minc_h.sv` (decode logic), plus `Hardware.md` (instruction table docs).

## Build and test commands

```sh
make            # builds target/mincc.exe and target/mincasm.exe from mincc/ and mincasm/
make clean      # removes target/ and *.o
make test       # clean + build + runs tests/test.py (full pipeline test)
python3 tests/test.py   # run tests directly (requires target/mincc and target/mincasm already built)
```

`tests/test.py` (via `tests/testfuncs.py`) drives the full toolchain per case: `mincc` (C → asm) → `mincasm` (asm → hex) → write `verilog/test.hex` → `iverilog`/`vvp` simulate a core (default `verilog/minc_h.sv`) + `verilog/minc_tb.sv` (compiled with `-DTEST -DVERBOSE -DSIM`) → assert the resulting TOP-of-stack/PORTA/SP register values. This requires `iverilog` and `vvp` (Icarus Verilog) on PATH in addition to gcc. There is no test runner for isolated unit tests — every case is an end-to-end compile+assemble+simulate round trip; add new cases as entries in the `E2E_CASES` dict in `tests/test.py` (tuples of `code, expected_top, kwargs` consumed by `tf.test_e2e`), or as standalone `tf.expect(...)` / `tf.expect_fail(...)` calls for assembler/compiler error cases.

`tf.test_e2e` / `tf.test_irq` in `tests/testfuncs.py` accept a `core:str = "minc_h.sv"` argument so a case can be pointed at a different core file. `tests/test_pipeline.py` (standalone — not wired into `make test`) uses this to run every `E2E_CASES` entry against all three cores in the tree (`minc_h.sv`, `minc_p2.sv`, `minc_p5.sv`) as a binary-compatibility check, then prints a per-case and total cycle-count comparison table; run it directly with `python3 tests/test_pipeline.py` (requires `target/mincc`/`target/mincasm` already built, same as `test.py`).

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

`ND_FUNC_DEF` generates each function body **twice** (`codegen.c`). All output goes through `emit()`, which the first pass mutes: that pass exists only to measure `cur_regstack_max`, the highest register the body reaches. The prologue then pushes `r<callee_save_lo()>..r<that>` in one place and the epilogue pops the same range, so the saves run exactly once per call. `push_regstack` itself emits nothing — it only records the high-water mark. This matters because it used to emit each save lazily at the register's first use: a first use inside a loop pushed once per iteration against a single pop (an infinite loop like `blink.c`'s then leaked the stack until it overran the globals), and a first use inside an untaken branch never pushed while the epilogue popped anyway. Seeding `cur_regstack_max` before the second pass is also what keeps every `return` consistent, since `ND_RETURN` emits its own copy of the epilogue. Because the prologue's saves sit above the frame-pointer load, `Y` points below them, so the epilogue restores `SP` from `Y` *before* popping — the body may leave `SP` anywhere. A consequence worth knowing when debugging: a stack imbalance inside a function is invisible at its return (`SP` is reloaded from `Y`), so it only shows up in a function that never returns.

Supported C subset (see `ast.h` grammar comment): `int`/`char`/`uint8_t` (char/uint8_t/int are all currently 1 byte — see `PTR_SIZE`/type handling in `nodes.h`), pointers (`*`, `&`), `if`/`else`, `for`, `while`, `break`, function calls/recursion, global and local variables, a `[[address=N]]` attribute for memory-mapped I/O variables (see `example/echoback.c`, `example/uart.c`), and a `[[isr]]`/`[[isr=N]]` attribute for interrupt handlers (see "CPU (verilog)" below).

There is no preprocessor and **no comment syntax at all** — neither `//` nor `/* */` tokenizes, so a comment anywhere in the input is an "Invalid token" error.

Two escape hatches out of that subset (both documented in `Hardware.md`): an `asm("...")` **statement** that emits its text verbatim into the assembly stream (adjacent string literals concatenate as in C; `\n` `\t` `\r` `\\` `\"` `\'` are decoded, `\0` is not), and `sei()`/`cli()` **builtins** that set/clear `PSR.IE`. There is no operand binding — an `asm` block cannot name a C variable, so values cross the boundary through `[[address=N]]` globals. Both lower to the same `ND_ASM` node (`new_asm_node` in `nodes.c`), which touches neither the regstack nor the type system, so it yields no value and cannot appear in an expression. `sei`/`cli` are resolved in `ident()` (`ast.c`) only when `find_name` finds nothing, so a user declaration of either name still shadows the builtin; `asm` is a real reserved word in `parse.c` and cannot be shadowed. The builtins expand to a read-modify-write of PSR rather than a blunt store so that PSR bit0 (the carry flag) survives — note the ISA's `stf`/`clf` mnemonics exist in `mincasm`'s table but are commented out of every core's decoder, so they assemble to a silent no-op and must not be used.

### Assembler (mincasm)

Single-pass, table-driven (`mincasm/main.c`). `g_inst_specs[]` maps each mnemonic to an `InstKind` (e.g. `INST_ALU_RR`, `INST_MVI`, `INST_REL16`, `INST_MEM_STORE`/`LOAD`) describing how operands are encoded. Labels are resolved via a backpatch pass: forward/unresolved references are recorded as `Fixup` entries and patched in a second loop after the whole input is read (supports both forward and backward label references, `jz`/`calr`/`jr` relative offsets). Output is one 5-hex-digit word per instruction line (18-bit instruction width), fed directly to Verilog's `$readmemh`.

### CPU (verilog)

**`verilog/minc_h.sv` is the reference CPU implementation** (18-bit instruction word, 4-state `S_FETCH → S_DECEXEC → S_MA → S_WB` non-pipelined state machine, memory-mapped SP at addresses `0x0000`/`0x0001`). `verilog/minc_p2.sv` and `verilog/minc_p5.sv` are binary-compatible pipelined reimplementations of the same ISA aimed at higher throughput — a 2-stage PicoBlaze-style Fetch/Execute core (structural stalls + always-flush on branches, no forwarding needed since only the Execute stage ever touches architectural state) and a classic 5-stage IF/ID/EX/MEM/WB core with EX/MEM- and MEM/WB-forwarding plus a one-cycle load-use stall, respectively (see the comment header of each file for its exact stage/hazard plan). `tests/test_pipeline.py` runs the full `E2E_CASES` suite against all three cores to check binary compatibility and compare cycle counts. `verilog/minc.sv` is a separate, older/legacy core (15-bit instruction word, different opcode map and state machine, not binary-compatible with the others) — check which core a task actually targets before editing; `minc_h.sv`/`minc_p2.sv`/`minc_p5.sv` are the ones under active development.

`minc_h.sv` also has single-level interrupt hardware (see `Hardware.md`'s "割り込み" section): 4 level-triggered, fixed-priority request lines (`irq_in[3:0]`, `irq_in[0]` highest), a `PSR` (carry + interrupt-enable, memory-mapped at `0x0002`) and a one-level auto-saved `PSR_SHADOW` (`0x0003`) that the `reti` instruction (identical to `ret` plus restoring `PSR` from the shadow) restores. `minc_p2.sv`/`minc_p5.sv` don't have this interrupt hardware (or the UART `INTR` pin wiring in `gowin/minc/src/minc_gw_top.sv`) yet — see `Hardware.md`'s "既知の制約". `verilog/minc_tb.sv`'s `irq_in` port/DUT wiring is `` `ifdef IRQ_TEST ``-guarded so the same testbench still builds against `minc_p2.sv`/`minc_p5.sv` via `tests/test_pipeline.py`; see `tests/fixtures/irq_vector.asm` + `tf.test_irq` in `tests/testfuncs.py` for the hand-assembled interrupt test harness (minc_h.sv only), and `tf.test_irq_e2e` for the mincc-compiled equivalent (below). `mincasm` supports an `.org <addr>` directive to pin instructions to fixed addresses, which `mincc` itself now uses too: a function defined with `[[isr=N]]` (N = 0-3) is auto-placed at IRQ vector N (instruction address `0x0001+N`) via a `.org`-based vector table that `mincc/main.c` emits ahead of a relocated crt0 (`.org 0x0005`) — but only when at least one `[[isr=N]]` function exists in the program, so ordinary (non-interrupt) programs' output, and the checked-in `example/*.hex`, are unaffected. Unclaimed vector slots are filled with `reti` (a safe no-op resume for a spurious `irq_in` pulse, since minc_h.sv still pushes/jumps unconditionally on any asserted line). Interrupts are masked out of reset (`psr` resets to `2'b00`) and crt0 does **not** arm them, so an `[[isr=N]]` program must call `sei()` itself — see `example/blink.c`. Because `minc_h.sv` does **not** auto-save general-purpose registers on interrupt entry (see Hardware.md's "既知の制約"), `mincc` reactively protects every register an ISR body actually touches (see `cur_is_isr` branches in `push_regstack` in `codegen.c`), plus a single X-pointer (r12:r13) save/restore bracketing the whole body in `generate_isr_prologue`/`generate_isr_epilogue` (`isr_x_save`/`isr_x_restore`) — this is unrelated to, and stricter than, the normal r0-r5-caller-saved/r6+-callee-saved convention, since an interrupt has no software caller to have protected anything. `[[isr]]` without `=N` still compiles a correctly-shaped handler (ends in `reti`, same register protection) but isn't auto-placed — wireable by hand exactly like `tests/fixtures/irq_vector.asm`. ISR functions may not take parameters, may not `return` a value, and may not be called directly (all enforced at parse/codegen time in `ast.c`/`codegen.c`).

Instruction decode centers on `op6`/`op4`/`op2` (top bits of the instruction) — see the `is_*` wires in `minc_h.sv` for the opcode map, and `Hardware.md` for the mnemonic/encoding table. 16 general-purpose 8-bit registers (`r0`–`r15`); `r12`–`r15` double as pointer/index register pairs (X = r12:r13, Y = r14:r15) for `stm`/`ldm` addressing.

`verilog/srom.sv` / `ssram.sv` are simple synthesizable ROM/RAM models used for simulation; `minc_tb.sv` also memory-maps a "port A" GPIO at addresses `0x0004`–`0x0006` for I/O testing.

### FPGA target (gowin/)

`gowin/minc/` is a Gowin IDE project (for an actual FPGA build/synthesis) wrapping the core in `gowin/minc/src/minc_gw_top.sv`, which adds a Gowin block-RAM instance plus memory-mapped peripherals: a UART (`uart_master/`), an I2C master (`i2c_master/`, mapped at `0x0010`-`0x0017`), and an 8-bit interval timer (`gowin/ips/timer/timer8.sv`, mapped at `0x0018`-`0x001F`, `O_COMPARE` wired to `irq_line[0]`). `UART`, `PORTA`, `WAIT` (wait-state support), `I2C`, and `TIMER8` are toggled via `` `define `` near the top of `minc_gw_top.sv` — enabling `UART` auto-enables `WAIT` since the UART core needs wait-state support to throttle CPU memory access. Currently `UART` is commented out (disabled) while `PORTA`/`WAIT`/`I2C`/`TIMER8` are enabled — check these defines before assuming a peripheral is live.

## Notes

- `target/`, `mincc/*.o`, `mincasm/*.o`, and `verilog/*.vcd`/`*.exe` are build artifacts.
- `example/*.hex` and `verilog/program.hex` are pre-built hex outputs checked in for convenience/reference — regenerate them via the pipeline above if the corresponding `.c`/`.asm` source changes.
- Root-level `temp*`/`tmp*` paths (gitignored via `temp**`/`tmp**` in `.gitignore`) are ad hoc scratch files (one-off `.c`/`.asm` experiments, sim logs) — not part of the maintained build.
