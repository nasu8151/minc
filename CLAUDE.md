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
python3 tests/test_m16.py datapath   # one case, by its key in CASES or C_CASES
```

`tests/test_m16.py` holds two dicts: `CASES` (hand-written assembly fixtures in `tests/fixtures/m16/*.asm`, aimed at the ISA/core) and `C_CASES` (full `mincc -16` → `mincasm -16` → `minc_16.sv` round trips, the minc-16 counterpart of `test.py`'s `E2E_CASES`). A `C_CASES` value is `(code, expected_top, kwargs)` where kwargs may carry `porta`, `irq` (a dict of `+irq_*` plusargs) and `verbose`; every case additionally asserts `SP == 0xFFFE`.

Pass `{"verbose": True}` in a case's kwargs (or `verbose=True` to `tf.test_e2e`) to dump the per-cycle register trace from the simulator.

### What a test actually does

`tests/test.py` (via `tests/testfuncs.py`) drives the full toolchain per case: `mincc` (C → asm) → `mincasm` (asm → hex) → write `verilog/test.hex` → `iverilog`/`vvp` simulate a core (default `verilog/minc_h.sv`) + `verilog/minc_tb.sv` (compiled with `-DTEST -DVERBOSE -DSIM`) → assert the resulting TOP-of-stack/PORTA/SP register values. Every case asserts `SP == 0xFFFE` at the end as a stack-balance check. This requires `iverilog` and `vvp` (Icarus Verilog) on PATH in addition to gcc. There is no test runner for isolated unit tests — every case is an end-to-end compile+assemble+simulate round trip; add new cases as entries in the `E2E_CASES` dict in `tests/test.py` (values are `(code, expected_top, kwargs)` tuples consumed by `tf.test_e2e`; `expected_top = -1` means "expect TOP to be `xx`/undefined"), or as standalone `tf.expect(...)` / `tf.expect_fail(...)` calls for assembler/compiler error cases.

`tf.test_e2e` / `tf.test_irq` / `tf.test_irq_e2e` all accept a `core:str = "minc_h.sv"` argument so a case can be pointed at a different core file — see the core table under "CPU (verilog)" for which cores that actually work with it.
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

**Two back ends.** `mincc/codegen.c` emits minc-8; `mincc/codegen_16.c` emits minc-16, selected by `mincc -16` (mirroring `mincasm -16`). They are separate ISAs, not modes, so `codegen_16.c` is a parallel implementation rather than a parameterization — see "minc-16 codegen" below. Everything in `codegen_16.c` is `static` except `m16_generate_top()` (the two files define the same function names, so this is what keeps them linkable side by side), and `codegen_16.h` deliberately does *not* include `codegen.h`/`ast.h` for the same reason. The front end is shared; the handful of parse-time decisions that differ are gated on the global `int g_m16` (declared in `nodes.h`, defined in `nodes.c`, set by `main.c`): stack/global slot alignment (`var_slot_size()` in `nodes.c`), the global-address bump pointer's stride (`ast.c`), and the `sei()`/`cli()` expansion (`ast.c`). With `g_m16 == 0` every one of those is the identity, so minc-8's output is byte-for-byte what it was — worth re-checking with a baseline binary after touching any of them.

Calling convention (see comment in `codegen.h`): arguments are passed in `r2..`; `r0`–`r5` are caller-saved, `r6`+ callee-saved; expression codegen treats `r2` upward as an implicit operand stack.

**Type sizes and the size-parameterized codegen.** Sizes are assigned in `type()` in `parse.c`: `char`/`uint8_t` = 1 byte, `int` = **2 bytes**, pointers = 2 (`PTR_SIZE`), `void` = 0. `char` is currently *unsigned* (see the `negativebrace` test case). Note that the "uint8_t, int and char mean the same (1 byte int) type" comments in `ast.h`'s grammar block and at `parse.c:43` are **stale** — `int` has been 16-bit since the 16-bit-compare work; trust `parse.c`'s `type->size` assignments, not those comments.

Because the machine is 8-bit, a 16-bit value lives in a *register pair* on the regstack. This is threaded through codegen as an explicit width: `generate(Node*, int expected_size)` returns the size it actually produced, `push_regstack(size)`/`pop_regstack(size)` reserve 1 or 2 consecutive registers, and `gen_i8`/`gen_i16` emit the 8- vs 16-bit form of an operation (16-bit arithmetic is add/adc, sub/sbc, lt/ltc pairs over the low/high halves). Mixed-width expressions are reconciled by `cast_i8_to_i16`/`cast_i16_to_i8`. When adding an operator, it generally needs *both* an 8-bit and a 16-bit lowering.

Supported C subset (see `ast.h` grammar comment, but note it lags the implementation): `int`/`char`/`uint8_t`/`void`, pointers (`*`, `&`, including pointer-to-pointer), arithmetic (`+ - *`), bitwise (`& | ^ ~`), comparison (`== != < <= > >=`), logical (`&& || !`, distinct from the bitwise ops), `if`/`else`, `for`, `while`, `break`, function calls/recursion, global and local variables with block scoping, a `[[address=N]]` attribute for memory-mapped I/O variables (see `example/echoback.c`, `example/uart.c`), and a `[[isr]]`/`[[isr=N]]` attribute for interrupt handlers (see "CPU (verilog)" below). Notably absent: arrays/subscripting, `struct`, division, and `switch`.
`ND_FUNC_DEF` generates each function body **twice** (`codegen.c`). All output goes through `emit()`, which the first pass mutes: that pass exists only to measure `cur_regstack_max`, the highest register the body reaches. The prologue then pushes `r<callee_save_lo()>..r<that>` in one place and the epilogue pops the same range, so the saves run exactly once per call. `push_regstack` itself emits nothing — it only records the high-water mark. This matters because it used to emit each save lazily at the register's first use: a first use inside a loop pushed once per iteration against a single pop (an infinite loop like `blink.c`'s then leaked the stack until it overran the globals), and a first use inside an untaken branch never pushed while the epilogue popped anyway. Seeding `cur_regstack_max` before the second pass is also what keeps every `return` consistent, since `ND_RETURN` emits its own copy of the epilogue. Because the prologue's saves sit above the frame-pointer load, `Y` points below them, so the epilogue restores `SP` from `Y` *before* popping — the body may leave `SP` anywhere. A consequence worth knowing when debugging: a stack imbalance inside a function is invisible at its return (`SP` is reloaded from `Y`), so it only shows up in a function that never returns.

Supported C subset (see `ast.h` grammar comment): `int`/`char`/`uint8_t` (char/uint8_t/int are all currently 1 byte — see `PTR_SIZE`/type handling in `nodes.h`), pointers (`*`, `&`), `if`/`else`, `for`, `while`, `break`, function calls/recursion, global and local variables, a `[[address=N]]` attribute for memory-mapped I/O variables (see `example/echoback.c`, `example/uart.c`), and a `[[isr]]`/`[[isr=N]]` attribute for interrupt handlers (see "CPU (verilog)" below).

There is no preprocessor and **no comment syntax at all** — neither `//` nor `/* */` tokenizes, so a comment anywhere in the input is an "Invalid token" error.

Two escape hatches out of that subset (both documented in `Hardware.md`): an `asm("...")` **statement** that emits its text verbatim into the assembly stream (adjacent string literals concatenate as in C; `\n` `\t` `\r` `\\` `\"` `\'` are decoded, `\0` is not), and `sei()`/`cli()` **builtins** that set/clear `PSR.IE`. There is no operand binding — an `asm` block cannot name a C variable, so values cross the boundary through `[[address=N]]` globals. Both lower to the same `ND_ASM` node (`new_asm_node` in `nodes.c`), which touches neither the regstack nor the type system, so it yields no value and cannot appear in an expression. `sei`/`cli` are resolved in `ident()` (`ast.c`) only when `find_name` finds nothing, so a user declaration of either name still shadows the builtin; `asm` is a real reserved word in `parse.c` and cannot be shadowed. The builtins expand to a read-modify-write of PSR rather than a blunt store so that PSR bit0 (the carry flag) survives — note the ISA's `stf`/`clf` mnemonics exist in `mincasm`'s table but are commented out of every core's decoder, so they assemble to a silent no-op and must not be used.

### minc-16 codegen (`mincc/codegen_16.c`)

Structurally a sibling of `codegen.c` — same regstack-as-expression-stack scheme, same two-pass `ND_FUNC_DEF` (a muted dry run measures `cur_regstack_max`, then the prologue/epilogue push and pop exactly `r<callee_save_lo()>..r<high-water>`) — but the size machinery is gone: **every C value minc-16 supports (`char`, `int`, any pointer) fits in one 16-bit register**, so a regstack slot is always one register, there is no `gen_i8`/`gen_i16` split, and `size` survives only to pick the memory access width (`ldb`/`stb` vs `ldw`/`stw`). Widening and narrowing conversions are free, i.e. `char` is 8-bit *in memory only* and gets promoted on load — so results minc-8 wraps at 8 bits do not wrap here (the `negative` case in `C_CASES` pins that difference down).

ABI (also in `codegen_16.h` and Hardware.md's minc-16 ABI table): r0 = return value **and** the address scratch, r1–r5 caller-saved with r1 the bottom of the expression stack, r6–r13 callee-saved, r14 = BP, r15 = SP (hardware). Things worth knowing before editing:

- **r0 is the only scratch.** `emit_load_const`/`emit_add_const`/`emit_mem_disp`/`emit_mem_abs` fall back to it whenever a constant doesn't fit its field: `[rB+n]` carries a signed 6-bit displacement, `mvi`/`addi` a sign-extended 8-bit immediate, and the absolute load/store form only reaches `0x00`–`0xFF` (so every auto-allocated global — those start at `0x100` — goes through a materialized pointer, while `[[address=N]]` MMIO under 256 uses the cheap absolute form). r0 is dead at every statement boundary, which is what makes this safe; an ISR prologue saves r0/r1 unconditionally because `sei()`/`cli()` and inline asm clobber both.
- **Frames must stay word-aligned** — 16-bit accesses ignore address bit 0, so an `int` at an odd offset reads the wrong word entirely. `var_slot_size()` in `nodes.c` pads each slot; `align_frame()` in the prologue is the other half.
- Three things `codegen.c` gets wrong are fixed here rather than mirrored, so don't "restore parity" by copying minc-8's version back: `break` uses a **stack** of loop-end labels (minc-8's single `current_end` global means a `break` after a nested loop jumps to the *inner* loop's end — see the `break-nested` case); `ND_FUNC_CALL` saves the **live** caller-saved range up front (minc-8 pushes each argument register just before the argument that lands in it, by which point an earlier argument's evaluation has already clobbered it — see `call-live-reg`); and `push_regstack` errors out instead of silently spilling the expression stack into r14/r15.
- `jz`/`jnz` take a register directly (`jz rs,label`), so minc-8's `or`/`chz` + `jz` condition dance collapses to one instruction — but the offset is only 8-bit relative.

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

`minc_16.sv` has the same interrupt hardware as `minc_h.sv` (4 fixed-priority `irq_in` lines, vectors at instruction addresses `0x0001`–`0x0004`, PSR/PSR_SHADOW as the low/high byte of data word 1 — i.e. bytes `0x0002`/`0x0003` — restored by `reti`), so `mincc -16` emits the same `.org`-based vector table for `[[isr=N]]` functions.

The `-DVERBOSE` caveat matters: `minc_tb.sv`'s verbose trace block reads `uut.state`, a signal only the non-pipelined cores have, so building `minc_p2.sv`/`minc_p5.sv` with `-DVERBOSE` fails elaboration with `Unable to bind wire/reg/memory 'uut.state'`. Since `tf.test_e2e` hardcodes `-DVERBOSE`, **`tests/test_pipeline.py` (which runs the E2E suite against p2/p5 and prints a cycle-count comparison table) does not currently run** — besides the `-DVERBOSE` clash, its `run_comparison()` still unpacks `E2E_CASES` as a list of tuples although it became a dict. Fixing that script means making the `-DVERBOSE` flag conditional in `testfuncs.py` and iterating `E2E_CASES.items()`. Building the pipelined cores by hand works fine:

```sh
cd verilog && iverilog -o /tmp/test_p5.out -g2012 -DTEST -DSIM minc_p5.sv minc_tb.sv && vvp /tmp/test_p5.out
```

`minc_h.sv` also has single-level interrupt hardware (see `Hardware.md`'s "割り込み" section): 4 level-triggered, fixed-priority request lines (`irq_in[3:0]`, `irq_in[0]` highest), a `PSR` (carry + interrupt-enable, memory-mapped at `0x0002`) and a one-level auto-saved `PSR_SHADOW` (`0x0003`) that the `reti` instruction (identical to `ret` plus restoring `PSR` from the shadow) restores. `verilog/minc_tb.sv`'s `irq_in` port/DUT wiring is `` `ifdef IRQ_TEST ``-guarded because `minc_p2.sv`/`minc_p5.sv` have no `irq_in` port at all (interrupts on the pipelined cores are listed as unimplemented in `Hardware.md`'s "既知の制約"), so the guard keeps one testbench buildable against every core. Under `-DIRQ_TEST` the testbench drives `irq_in` from `+irq_cycle=` / `+irq_mask=` / `+irq_len=` / `+irq_period=` plusargs (`irq_period=0` = one-shot pulse; `>0` = re-pulse every N cycles, which is what proves `reti` restores IE). See `tests/fixtures/irq_vector.asm` + `tf.test_irq` in `tests/testfuncs.py` for the hand-assembled interrupt test harness, and `tf.test_irq_e2e` for the mincc-compiled equivalent (below). `mincasm` supports an `.org <addr>` directive to pin instructions to fixed addresses, which `mincc` itself now uses too: a function defined with `[[isr=N]]` (N = 0-3) is auto-placed at IRQ vector N (instruction address `0x0001+N`) via a `.org`-based vector table that `mincc/main.c` emits ahead of a relocated crt0 (`.org 0x0005`) — but only when at least one `[[isr=N]]` function exists in the program, so ordinary (non-interrupt) programs' output, and the checked-in `example/*.hex`, are unaffected. Unclaimed vector slots are filled with `reti` (a safe no-op resume for a spurious `irq_in` pulse, since minc_h.sv still pushes/jumps unconditionally on any asserted line). Because `minc_h.sv` does **not** auto-save general-purpose registers on interrupt entry (see Hardware.md's "既知の制約"), `mincc` reactively protects every register an ISR body actually touches (see `cur_is_isr` branches in `push_regstack` in `codegen.c`), plus a single X-pointer (r12:r13) save/restore bracketing the whole body in `generate_isr_prologue`/`generate_isr_epilogue` (`isr_x_save`/`isr_x_restore`) — this is unrelated to, and stricter than, the normal r0-r5-caller-saved/r6+-callee-saved convention, since an interrupt has no software caller to have protected anything. `[[isr]]` without `=N` still compiles a correctly-shaped handler (ends in `reti`, same register protection) but isn't auto-placed — wireable by hand exactly like `tests/fixtures/irq_vector.asm`. ISR functions may not take parameters, may not `return` a value, and may not be called directly (all enforced at parse/codegen time in `ast.c`/`codegen.c`).
**`verilog/minc_h.sv` is the reference CPU implementation** (18-bit instruction word, 4-state `S_FETCH → S_DECEXEC → S_MA → S_WB` non-pipelined state machine, memory-mapped SP at addresses `0x0000`/`0x0001`). `verilog/minc_p2.sv` and `verilog/minc_p5.sv` are binary-compatible pipelined reimplementations of the same ISA aimed at higher throughput — a 2-stage PicoBlaze-style Fetch/Execute core (structural stalls + always-flush on branches, no forwarding needed since only the Execute stage ever touches architectural state) and a classic 5-stage IF/ID/EX/MEM/WB core with EX/MEM- and MEM/WB-forwarding plus a one-cycle load-use stall, respectively (see the comment header of each file for its exact stage/hazard plan). `tests/test_pipeline.py` runs the full `E2E_CASES` suite against all three cores to check binary compatibility and compare cycle counts. `verilog/minc.sv` is a separate, older/legacy core (15-bit instruction word, different opcode map and state machine, not binary-compatible with the others) — check which core a task actually targets before editing; `minc_h.sv`/`minc_p2.sv`/`minc_p5.sv` are the ones under active development.

`minc_h.sv` also has single-level interrupt hardware (see `Hardware.md`'s "割り込み" section): 4 level-triggered, fixed-priority request lines (`irq_in[3:0]`, `irq_in[0]` highest), a `PSR` (carry + interrupt-enable, memory-mapped at `0x0002`) and a one-level auto-saved `PSR_SHADOW` (`0x0003`) that the `reti` instruction (identical to `ret` plus restoring `PSR` from the shadow) restores. `minc_p2.sv`/`minc_p5.sv` don't have this interrupt hardware (or the UART `INTR` pin wiring in `gowin/minc/src/minc_gw_top.sv`) yet — see `Hardware.md`'s "既知の制約". `verilog/minc_tb.sv`'s `irq_in` port/DUT wiring is `` `ifdef IRQ_TEST ``-guarded so the same testbench still builds against `minc_p2.sv`/`minc_p5.sv` via `tests/test_pipeline.py`; see `tests/fixtures/irq_vector.asm` + `tf.test_irq` in `tests/testfuncs.py` for the hand-assembled interrupt test harness (minc_h.sv only), and `tf.test_irq_e2e` for the mincc-compiled equivalent (below). `mincasm` supports an `.org <addr>` directive to pin instructions to fixed addresses, which `mincc` itself now uses too: a function defined with `[[isr=N]]` (N = 0-3) is auto-placed at IRQ vector N (instruction address `0x0001+N`) via a `.org`-based vector table that `mincc/main.c` emits ahead of a relocated crt0 (`.org 0x0005`) — but only when at least one `[[isr=N]]` function exists in the program, so ordinary (non-interrupt) programs' output, and the checked-in `example/*.hex`, are unaffected. Unclaimed vector slots are filled with `reti` (a safe no-op resume for a spurious `irq_in` pulse, since minc_h.sv still pushes/jumps unconditionally on any asserted line). Interrupts are masked out of reset (`psr` resets to `2'b00`) and crt0 does **not** arm them, so an `[[isr=N]]` program must call `sei()` itself — see `example/blink.c`. Because `minc_h.sv` does **not** auto-save general-purpose registers on interrupt entry (see Hardware.md's "既知の制約"), `mincc` reactively protects every register an ISR body actually touches (see `cur_is_isr` branches in `push_regstack` in `codegen.c`), plus a single X-pointer (r12:r13) save/restore bracketing the whole body in `generate_isr_prologue`/`generate_isr_epilogue` (`isr_x_save`/`isr_x_restore`) — this is unrelated to, and stricter than, the normal r0-r5-caller-saved/r6+-callee-saved convention, since an interrupt has no software caller to have protected anything. `[[isr]]` without `=N` still compiles a correctly-shaped handler (ends in `reti`, same register protection) but isn't auto-placed — wireable by hand exactly like `tests/fixtures/irq_vector.asm`. ISR functions may not take parameters, may not `return` a value, and may not be called directly (all enforced at parse/codegen time in `ast.c`/`codegen.c`).

Instruction decode centers on `op6`/`op4`/`op2` (top bits of the instruction) — see the `is_*` wires in `minc_h.sv` for the opcode map, and `Hardware.md` for the mnemonic/encoding table. 16 general-purpose 8-bit registers (`r0`–`r15`); `r12`–`r15` double as pointer/index register pairs (X = r12:r13, Y = r14:r15) for `stm`/`ldm` addressing.

**minc-16** (`verilog/minc_16.sv`, module `minc16`) is a *separate ISA*, not a variant of the above — spec and rationale live in `Hardware.md`'s "### minc-16" and "## minc-16 設計メモ" sections. It keeps the 18-bit instruction word and the 4-state FSM, but registers/ALU are 16-bit, the data space is byte-addressed over a 16-bit bus with byte-lane write enables (`we[1:0]`), SP is the general-purpose register `r15` (BP=r14 is software convention only, not hardware), and address calculation plus SP±2 share the one ALU instead of using dedicated adders. It is **not** drop-in swappable with the other cores — different port list — hence the distinct module name and its own testbench `minc16_tb.sv` + `ssram16.sv`. Assemble with `./target/mincasm -16` (the `-16` flag swaps in `g_inst_specs16` and a separate set of `enc16_*` encoders in `mincasm/main.c`) and compile with `./target/mincc -16` (see "minc-16 codegen" above). Tests are a mix of hand-written asm under `tests/fixtures/m16/` and compiled C, both driven by `tests/test_m16.py`.

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
