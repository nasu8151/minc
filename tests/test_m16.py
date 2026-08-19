#!/usr/bin/env python3
"""minc-16 test runner.

minc-16 is a separate ISA from minc-8 (see Hardware.md "### minc-16"), so it gets
its own runner: `mincasm -16` for the assembler, minc_16.sv + minc16_tb.sv for the
simulation. mincc does not target minc-16 yet, so every case here is hand-written
assembly rather than a compiled C round trip.

Run from the repo root:
    python3 tests/test_m16.py            # everything
    python3 tests/test_m16.py datapath   # one case, by its key in CASES
"""

import subprocess
import sys
import os

FIXTURES = "tests/fixtures/m16"
CORE = "minc_16.sv"
TB = "minc16_tb.sv"

# key -> (fixture, expected_top, expected_sp, irq kwargs or None)
CASES = {
    # Immediates (incl. sign-extended mvi + mvih), 16-bit add/addi, push/pop
    # round trip, word and byte loads through the displacement mode.
    "datapath": ("datapath.asm", 0x3410, 0xFFFE, None),
    # calr/ret (single-word return address), jz/jnz, both byte lanes, absolute
    # addressing, negative displacement, 16-bit unsigned lt.
    "control": ("control.asm", 0x0037, 0xFFFE, None),
    # One-shot IRQ: exactly one ISR entry, registers preserved, IE restored.
    "irq_oneshot": ("irq_oneshot.asm", 0x0001, 0xFFFE,
                    {"irq_cycle": 60, "irq_mask": 1, "irq_len": 6, "irq_period": 0}),
    # Re-pulsing IRQ: proves reti re-arms IE, and that a PSR_SHADOW write
    # changes what reti restores.
    "irq_periodic": ("irq_periodic.asm", 0x0003, 0xFFFE,
                     {"irq_cycle": 60, "irq_mask": 1, "irq_len": 6, "irq_period": 40}),
}


def build(irq: bool, out: str):
    defines = ["-DTEST", "-DVERBOSE", "-DSIM"]
    if irq:
        defines.append("-DIRQ_TEST")
    r = subprocess.run(["iverilog", "-o", out, "-g2012"] + defines + [CORE, TB],
                       cwd="./verilog", capture_output=True, text=True)
    if r.returncode != 0:
        raise Exception(f"iverilog failed:\n{r.stderr.strip()}")


def run_case(key: str, verbose: bool = False):
    fixture, expected_top, expected_sp, irq = CASES[key]
    with open(os.path.join(FIXTURES, fixture)) as f:
        asm = f.read()

    asm_run = subprocess.run(["./target/mincasm", "-16"], input=asm,
                             capture_output=True, text=True)
    if asm_run.returncode != 0:
        raise Exception(f"[{key}] mincasm -16 failed:\n{asm_run.stderr.strip()}")
    with open("verilog/test.hex", "w") as f:
        f.write(asm_run.stdout)

    out = "__minc16_irq.out" if irq else "__minc16_test.out"
    build(irq is not None, out)

    argv = ["vvp", f"./{out}"]
    if irq:
        argv += [f"+{k}={v}" for k, v in irq.items()]
    sim = subprocess.run(argv, cwd="./verilog", capture_output=True, text=True)
    if sim.returncode != 0:
        raise Exception(f"[{key}] simulation failed:\n{sim.stderr.strip()}")
    if verbose:
        print(sim.stdout)

    lines = sim.stdout.strip().splitlines()
    if len(lines) < 2:
        raise Exception(f"[{key}] no output from simulation")
    _, top_str, sp_str = lines[-2].split(", ")
    cycles_str = lines[-1]
    cycles = int(cycles_str.split(":")[1].strip()) if ":" in cycles_str else None
    top = int(top_str.split(":")[1].strip(), 16)
    sp = int(sp_str.split(":")[1].strip(), 16)

    assert top == expected_top, \
        f"[FAIL] {key}: expected TOP {expected_top:#06x}, got {top:#06x}"
    assert sp == expected_sp, \
        f"[FAIL] {key}: expected SP {expected_sp:#06x}, got {sp:#06x}"
    print(f"[OK] minc-16 {key:<13} => TOP: {top:#06x}, SP: {sp:#06x} ({CORE}, {cycles} cycles)")


if __name__ == "__main__":
    keys = list(CASES)
    if len(sys.argv) >= 2:
        if sys.argv[1] not in CASES:
            print(f"Unknown case '{sys.argv[1]}'. Available: {', '.join(CASES)}")
            sys.exit(1)
        keys = [sys.argv[1]]

    for k in keys:
        run_case(k)
    print("\n[OK] [ALL minc-16 TESTS PASSED]")
