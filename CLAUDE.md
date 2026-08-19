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
make            # builds target/mincc and target/mincasm from mincc/ and mincasm/
make clean      # removes target/ and *.o
make test       # clean + build + runs tests/test.py (full pipeline test)
python3 tests/test.py   # run tests directly (requires target/mincc and target/mincasm already built)
```

All test commands must be run **from the repo root** — `tests/testfuncs.py` invokes `./target/mincc` and writes `verilog/test.hex` via paths relative to the cwd.

### Running a subset of the tests

`tests/test.py` takes an optional argument (the compiler/assembler `expect_fail` error cases always run first regardless):

```sh
python3 tests/test.py            # everything: E2E suite + IRQ suite
python3 tests/test.py basic      # one E2E case, by its key in the E2E_CASES dict
python3 tests/test.py fib        # ...any other E2E_CASES key
python3 tests/test.py irq        # only the interrupt/ISR suite (run_irq_tests)
```

`tests/test.py` covers **minc-8 only**. minc-16 has its own runner (`make test` does not invoke it):

```sh
python3 tests/test_m16.py            # all minc-16 cases
python3 tests/test_m16.py datapath   # one case, by its key in the CASES dict
```

Pass `{"verbose": True}` in a case's kwargs (or `verbose=True` to `tf.test_e2e`) to dump the per-cycle register trace from the simulator.

### What a test actually does

`tests/test.py` (via `tests/testfuncs.py`) drives the full toolchain per case: `mincc` (C → asm) → `mincasm` (asm → hex) → write `verilog/test.hex` → `iverilog`/`vvp` simulate a core (default `verilog/minc_h.sv`) + `verilog/minc_tb.sv` (compiled with `-DTEST -DVERBOSE -DSIM`) → assert the resulting TOP-of-stack/PORTA/SP register values. Every case asserts `SP == 0xFFFE` at the end as a stack-balance check. This requires `iverilog` and `vvp` (Icarus Verilog) on PATH in addition to gcc. There is no test runner for isolated unit tests — every case is an end-to-end compile+assemble+simulate round trip; add new cases as entries in the `E2E_CASES` dict in `tests/test.py` (values are `(code, expected_top, kwargs)` tuples consumed by `tf.test_e2e`; `expected_top = -1` means "expect TOP to be `xx`/undefined"), or as standalone `tf.expect(...)` / `tf.expect_fail(...)` calls for assembler/compiler error cases.

`tf.test_e2e` / `tf.test_irq` / `tf.test_irq_e2e` all accept a `core:str = "minc_h.sv"` argument so a case can be pointed at a different core file — see the core table under "CPU (verilog)" for which cores that actually work with it.

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

**Type sizes and the size-parameterized codegen.** Sizes are assigned in `type()` in `parse.c`: `char`/`uint8_t` = 1 byte, `int` = **2 bytes**, pointers = 2 (`PTR_SIZE`), `void` = 0. `char` is currently *unsigned* (see the `negativebrace` test case). Note that the "uint8_t, int and char mean the same (1 byte int) type" comments in `ast.h`'s grammar block and at `parse.c:43` are **stale** — `int` has been 16-bit since the 16-bit-compare work; trust `parse.c`'s `type->size` assignments, not those comments.

Because the machine is 8-bit, a 16-bit value lives in a *register pair* on the regstack. This is threaded through codegen as an explicit width: `generate(Node*, int expected_size)` returns the size it actually produced, `push_regstack(size)`/`pop_regstack(size)` reserve 1 or 2 consecutive registers, and `gen_i8`/`gen_i16` emit the 8- vs 16-bit form of an operation (16-bit arithmetic is add/adc, sub/sbc, lt/ltc pairs over the low/high halves). Mixed-width expressions are reconciled by `cast_i8_to_i16`/`cast_i16_to_i8`. When adding an operator, it generally needs *both* an 8-bit and a 16-bit lowering.

Supported C subset (see `ast.h` grammar comment, but note it lags the implementation): `int`/`char`/`uint8_t`/`void`, pointers (`*`, `&`, including pointer-to-pointer), arithmetic (`+ - *`), bitwise (`& | ^ ~`), comparison (`== != < <= > >=`), logical (`&& || !`, distinct from the bitwise ops), `if`/`else`, `for`, `while`, `break`, function calls/recursion, global and local variables with block scoping, a `[[address=N]]` attribute for memory-mapped I/O variables (see `example/echoback.c`, `example/uart.c`), and a `[[isr]]`/`[[isr=N]]` attribute for interrupt handlers (see "CPU (verilog)" below). Notably absent: arrays/subscripting, `struct`, division, and `switch`.

### Assembler (mincasm)

Single-pass, table-driven (`mincasm/main.c`). `g_inst_specs[]` maps each mnemonic to an `InstKind` (e.g. `INST_ALU_RR`, `INST_MVI`, `INST_REL16`, `INST_MEM_STORE`/`LOAD`) describing how operands are encoded. Labels are resolved via a backpatch pass: forward/unresolved references are recorded as `Fixup` entries and patched in a second loop after the whole input is read (supports both forward and backward label references, `jz`/`calr`/`jr` relative offsets). Output is one 5-hex-digit word per instruction line (18-bit instruction width), fed directly to Verilog's `$readmemh`.

### CPU (verilog)

**`verilog/minc_h.sv` is the reference CPU implementation** (18-bit instruction word, 4-state `S_FETCH → S_DECEXEC → S_MA → S_WB` state machine, memory-mapped SP at addresses `0x0000`/`0x0001`) and the one that gets active development. Several other cores share the `minc` module name and the same port list, so they are drop-in swaps at `iverilog` invocation time — but they are **not** interchangeable in practice; check which one a task targets before editing:

| file | what it is | usable with `minc_tb.sv`? |
| --- | --- | --- |
| `minc_h.sv` | reference core, 18-bit insn, has `irq_in` | yes — the default everywhere |
| `minc.sv` | legacy core, 15-bit insn, different opcode map/state machine | no (different ISA) |
| `minc_p2.sv` | 2-stage pipeline (Fetch//Execute), binary-compatible with `minc_h.sv`, **no `irq_in`** | only without `-DVERBOSE` |
| `minc_p5.sv` | 5-stage pipeline (IF/ID/EX/MEM/WB) with forwarding, binary-compatible, **no `irq_in`** | only without `-DVERBOSE` |
| `minc_16.sv` | **minc-16**: separate ISA — 16-bit regs/ALU, byte-addressed 16-bit data bus, SP in the register file as r15. Module is named `minc16`, not `minc` | no — use `minc16_tb.sv` (see below) |

The `-DVERBOSE` caveat matters: `minc_tb.sv`'s verbose trace block reads `uut.state`, a signal only the non-pipelined cores have, so building `minc_p2.sv`/`minc_p5.sv` with `-DVERBOSE` fails elaboration with `Unable to bind wire/reg/memory 'uut.state'`. Since `tf.test_e2e` hardcodes `-DVERBOSE`, **`tests/test_pipeline.py` (which runs the E2E suite against p2/p5 and prints a cycle-count comparison table) does not currently run** — besides the `-DVERBOSE` clash, its `run_comparison()` still unpacks `E2E_CASES` as a list of tuples although it became a dict. Fixing that script means making the `-DVERBOSE` flag conditional in `testfuncs.py` and iterating `E2E_CASES.items()`. Building the pipelined cores by hand works fine:

```sh
cd verilog && iverilog -o /tmp/test_p5.out -g2012 -DTEST -DSIM minc_p5.sv minc_tb.sv && vvp /tmp/test_p5.out
```

`minc_h.sv` also has single-level interrupt hardware (see `Hardware.md`'s "割り込み" section): 4 level-triggered, fixed-priority request lines (`irq_in[3:0]`, `irq_in[0]` highest), a `PSR` (carry + interrupt-enable, memory-mapped at `0x0002`) and a one-level auto-saved `PSR_SHADOW` (`0x0003`) that the `reti` instruction (identical to `ret` plus restoring `PSR` from the shadow) restores. `verilog/minc_tb.sv`'s `irq_in` port/DUT wiring is `` `ifdef IRQ_TEST ``-guarded because `minc_p2.sv`/`minc_p5.sv` have no `irq_in` port at all (interrupts on the pipelined cores are listed as unimplemented in `Hardware.md`'s "既知の制約"), so the guard keeps one testbench buildable against every core. Under `-DIRQ_TEST` the testbench drives `irq_in` from `+irq_cycle=` / `+irq_mask=` / `+irq_len=` / `+irq_period=` plusargs (`irq_period=0` = one-shot pulse; `>0` = re-pulse every N cycles, which is what proves `reti` restores IE). See `tests/fixtures/irq_vector.asm` + `tf.test_irq` in `tests/testfuncs.py` for the hand-assembled interrupt test harness, and `tf.test_irq_e2e` for the mincc-compiled equivalent (below). `mincasm` supports an `.org <addr>` directive to pin instructions to fixed addresses, which `mincc` itself now uses too: a function defined with `[[isr=N]]` (N = 0-3) is auto-placed at IRQ vector N (instruction address `0x0001+N`) via a `.org`-based vector table that `mincc/main.c` emits ahead of a relocated crt0 (`.org 0x0005`) — but only when at least one `[[isr=N]]` function exists in the program, so ordinary (non-interrupt) programs' output, and the checked-in `example/*.hex`, are unaffected. Unclaimed vector slots are filled with `reti` (a safe no-op resume for a spurious `irq_in` pulse, since minc_h.sv still pushes/jumps unconditionally on any asserted line). Because `minc_h.sv` does **not** auto-save general-purpose registers on interrupt entry (see Hardware.md's "既知の制約"), `mincc` reactively protects every register an ISR body actually touches (see `cur_is_isr` branches in `push_regstack` in `codegen.c`), plus a single X-pointer (r12:r13) save/restore bracketing the whole body in `generate_isr_prologue`/`generate_isr_epilogue` (`isr_x_save`/`isr_x_restore`) — this is unrelated to, and stricter than, the normal r0-r5-caller-saved/r6+-callee-saved convention, since an interrupt has no software caller to have protected anything. `[[isr]]` without `=N` still compiles a correctly-shaped handler (ends in `reti`, same register protection) but isn't auto-placed — wireable by hand exactly like `tests/fixtures/irq_vector.asm`. ISR functions may not take parameters, may not `return` a value, and may not be called directly (all enforced at parse/codegen time in `ast.c`/`codegen.c`).

Instruction decode centers on `op6`/`op4`/`op2` (top bits of the instruction) — see the `is_*` wires in `minc_h.sv` for the opcode map, and `Hardware.md` for the mnemonic/encoding table. 16 general-purpose 8-bit registers (`r0`–`r15`); `r12`–`r15` double as pointer/index register pairs (X = r12:r13, Y = r14:r15) for `stm`/`ldm` addressing.

**minc-16** (`verilog/minc_16.sv`, module `minc16`) is a *separate ISA*, not a variant of the above — spec and rationale live in `Hardware.md`'s "### minc-16" and "## minc-16 設計メモ" sections. It keeps the 18-bit instruction word and the 4-state FSM, but registers/ALU are 16-bit, the data space is byte-addressed over a 16-bit bus with byte-lane write enables (`we[1:0]`), SP is the general-purpose register `r15` (BP=r14 / X=r13 are software convention only, not hardware), and address calculation plus SP±2 share the one ALU instead of using dedicated adders. It is **not** drop-in swappable with the other cores — different port list — hence the distinct module name and its own testbench `minc16_tb.sv` + `ssram16.sv`. Assemble with `./target/mincasm -16` (the `-16` flag swaps in `g_inst_specs16` and a separate set of `enc16_*` encoders in `mincasm/main.c`); `mincc` cannot target it yet, so all minc-16 tests are hand-written asm under `tests/fixtures/m16/`.

The one non-obvious invariant when editing `minc_16.sv`: **`servicing_irq` must take priority over every signal derived from `instr`** (`alu_op`, `alu_b`, `address`, `we`). The interrupt-entry pseudo-op runs with `instr` already holding the deferred instruction, and because the ALU is shared, that instruction's opcode would otherwise hijack the `SP -= 2` computation. `minc_h.sv` is immune because its SP has a dedicated adder. See the 【重要】 subsection in `Hardware.md`'s 設計メモ.

`verilog/srom.sv` / `ssram.sv` are simple synthesizable ROM/RAM models used for simulation; `minc_tb.sv` also memory-maps a "port A" GPIO at addresses `0x0004`–`0x0006` for I/O testing.

### FPGA target (gowin/)

`gowin/minc/` is a Gowin IDE project (for an actual FPGA build/synthesis) wrapping the core in `gowin/minc/src/minc_gw_top.sv`, which adds a Gowin block-RAM instance plus memory-mapped peripherals: a UART (`uart_master/`), an I2C master (`i2c_master/`, mapped at `0x0010`-`0x0017`), and an 8-bit interval timer (`gowin/ips/timer/timer8.sv`, mapped at `0x0018`-`0x001F`, `O_COMPARE` wired to `irq_line[0]`). `UART`, `PORTA`, `WAIT` (wait-state support), `I2C`, and `TIMER8` are toggled via `` `define `` near the top of `minc_gw_top.sv` — enabling `UART` auto-enables `WAIT` since the UART core needs wait-state support to throttle CPU memory access. All five are currently enabled — but these defines get toggled often, so read the top of `minc_gw_top.sv` before assuming a peripheral is live.

## Notes

- `target/`, `mincc/*.o`, `mincasm/*.o`, and `verilog/*.vcd`/`*.out` are build artifacts (`.gitignore` covers `*.o`, `*.out`, `*.vcd`, `*.exe`, `*.log`).
- Documentation lives in `Hardware.md` (Japanese: instruction/encoding table, interrupt spec, known limitations) and `Architecture_Comparison.md` (LUT-count rationale vs. other soft cores). `Hardware.md`'s instruction table is the thing to update when the ISA changes.
- Commit messages in this repo are Japanese-leaning and prefixed `feat:` / `fix:` / `add:`.
- `example/*.hex` and `verilog/program.hex` are pre-built hex outputs checked in for convenience/reference — regenerate them via the pipeline above if the corresponding `.c`/`.asm` source changes.
- Root-level `temp*`/`tmp*` paths (gitignored via `temp**`/`tmp**` in `.gitignore`) are ad hoc scratch files (one-off `.c`/`.asm` experiments, sim logs) — not part of the maintained build.
