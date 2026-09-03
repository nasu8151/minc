#!/usr/bin/env python3
"""minc-16 test runner.

minc-16 is a separate ISA from minc-8 (see Hardware.md "### minc-16"), so it gets
its own runner: `mincasm -16` for the assembler, minc_16.sv + minc16_tb.sv for the
simulation. Two kinds of case live here:

  CASES   -- hand-written assembly fixtures (tests/fixtures/m16/*.asm), aimed at
             the ISA and the core itself.
  C_CASES -- full C round trips through `mincc -16` (mincc/codegen_16.c), the
             minc-16 equivalent of tests/test.py's E2E_CASES.

Run from the repo root:
    python3 tests/test_m16.py            # everything
    python3 tests/test_m16.py datapath   # one case, by its key in CASES or C_CASES
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

# key -> (C source, expected_top, kwargs)
#
# Note on `char`: on minc-16 a char is 8-bit *in memory* only. Loading one
# zero-extends it into a full 16-bit register and nothing narrows it again until
# the next byte store, which is C's ordinary integer promotion -- so results that
# minc-8 wrapped at 8 bits do not wrap here. "negative" below is the deliberate
# demonstration of that difference; see mincc/codegen_16.h.
C_CASES = {
    "basic": ("char main(){return 1+2;}", 3, {}),
    "brace": ("char main(){return (1+2)*3;}", 9, {}),
    # minc-8 gives 0xEC here (char truncation); minc-16 promotes and keeps -20.
    "negative": ("char main(){return -(2+3)*4;}", 0xFFEC, {}),
    "bitmasks": ("char main(){return ~(((0b11111111 & 0b11000011) | 0b00001111) ^ 0b00111100);}",
                 0b00001100, {}),
    "equality": ("char main(){return (1+1==2)+(2*2!=5);}", 2, {}),
    "relational": ("char main(){return (3+2<6)+(3+3<=5)+(5>2+2)+(2+2>=5);}", 2, {}),
    # 16-bit values need no register pairs any more: one add, one sub.
    "int-arith": ("int main(){return 1919 + 4545;}", 6464, {}),
    "int-sub": ("int main(){int a = 1234; int b = 5678; return b - a;}", 4444, {}),
    "int-relational": ("int main(){return (5555<5556)+(666<=665)+(4445>4444)+(4444>=4445);}", 2, {}),
    # A char and an int local in the same frame: proves the -16 slot padding, since
    # an unpadded frame would put the int at an odd offset and ldw would silently
    # read the wrong word.
    "mixed-frame": ("int main(){char a = 3; int b = 5000; char c = 7; return b + a + c;}", 5010, {}),
    "localval": ("char main(){char a=3;return a+2;}", 5, {}),
    "val-arith": ("char main(){char a=1;char b=2;char c=3;return a + b * c;}", 7, {}),
    "if": ("char main(){char hoge=3;char fuga=hoge+2;if (fuga==5) return 42;return 0;}", 42, {}),
    "if-else": ("int main(){int h=3000;int f=h+2000;if (h==3001 && f==5000) return 42;"
                "else if (h==3000 && f==5000) return 46;return 0;}", 46, {}),
    "for": ("char main(){char sum=0;for(char i=1;i<5;i=i+1) sum=sum+i;return sum;}", 10, {}),
    "while": ("char main(){char i=0;while(i<3) i=i+1;return i;}", 3, {}),
    "return-in-while": ("char main(){char i=0;while(i<10){i=i+1;if(i==5){return 20*i;}}return 0;}",
                        100, {}),
    "break": ("char main(){char i=0;while(1){if(i==5) break;i=i+1;}return i;}", 5, {}),
    # `break` after an inner loop must leave the *outer* loop. minc-8's codegen
    # keeps one global "current end label", which the inner loop overwrites, so it
    # would jump back into the outer body and spin forever; codegen_16.c keeps a
    # stack of them. Without the fix this case hits the simulation timeout.
    "break-nested": ("char main(){char n=0;while(1){for(char i=0;i<3;i=i+1){n=n+1;}"
                     "if(n>=6) break;}return n;}", 6, {}),
    "global": ("char gvar=10;char main(){return gvar+5;}", 15, {}),
    # Two ints at file scope. minc-8 advances the global cursor one byte per
    # variable whatever its size, so these would overlap; -16 advances by the slot.
    "global-int-pair": ("int a=1000;int b=2345;char main(){return b-a;}", 1345, {}),
    "global-and-local": ("char a=1;char b=2;char main(){char c=3;return a+b+c;}", 6, {}),
    "global-loop": ("char g=0;char main(){while(g<21){for(char i=1;i<5;i=i+1) g=g+i;}return g;}",
                    30, {}),
    "call": ("char ret42(){return 42;}char main(){return ret42();}", 42, {}),
    "call-args": ("char mac(char a,char b,char c){return a*b+c;}char main(){return mac(2,3,4);}",
                  10, {}),
    # A live expression-stack register across a call: x must survive f(x+1).
    # minc-8 pushes each argument register just before the argument that lands in
    # it, by which point the previous argument's evaluation has already trampled
    # it; codegen_16.c saves the live set up front.
    "call-live-reg": ("char f(char a){return a*2;}char main(){char x=3;return x+f(x+1);}", 11, {}),
    "recursion": ("char arg=0;char fac(){char i=arg;if(i==0) return 1;arg=i-1;return fac()*i;}"
                  "char main(){arg=5;return fac();}", 120, {}),
    "fib": ("char fib(char i){if(i==0) return 0;if(i==1) return 1;char a=fib(i-1); \
             char b=fib(i-2);return a+b;}char main(){return fib(11);}", 89, {}),
    # Kept at 13 (not higher) because minc16_tb.sv gives up after 131071 cycles.
    "fib16": ("int fib(int i){if(i==0) return 0;if(i==1) return 1;int a=fib(i-1);"
              "int b=fib(i-2);return a+b;}int main(){return fib(13);}", 233, {}),
    "mixed-args": ("int add(char a, int b){int aa = a;return aa + b;}"
                   "int main(){char a = 107; int b = 1032; return add(a, b);}", 1139, {}),
    "pointer": ("char main(){char a = 3;char *b = &a;return *b;}", 3, {}),
    "pointer-int": ("int main(){int a = 1155; int *b = &a; int **c = &b; return **c;}", 1155, {}),
    "pointer-store": ("char main(){char a = 3;char *b = &a;*b = 5;return a;}", 5, {}),
    # `*p = e` must evaluate e exactly once: called once => v=1, n=1 => 11.
    "deref-assign-once": ("char n=0;char bump(){n=n+1;return n;}"
                          "char main(){char v=0;char *p=&v;*p=bump();return v*10+n;}", 11, {}),
    "pointer-global": ("int g=777;int main(){int *p=&g;*p=*p+3;return g;}", 780, {}),
    "logical": ("char main(){char a=1;char b=2;return ((a!=b)&&(a<b))+((a==b)||(a>b))"
                "+((!b==0)&&(!a==0));}", 2, {}),
    # Memory-mapped I/O through [[address=N]]: both bytes of word 1 in the
    # testbench's port A, reached with the 8-bit absolute byte form.
    "porta": ("char [[address=0x04]] port_a_out;char [[address=0x05]] port_a_dir;"
              "char main(){port_a_dir=0xFF;port_a_out=0x55;return 0;}", 0, {"porta": 0x55}),
    # Inline assembly, emitted verbatim -- minc-16 mnemonics, since it bypasses
    # codegen entirely.
    "asm": ("char [[address=0x05]] d;char main(){d=0xFF;asm(\"mvi r0,0x5A\\n\" \"stb 4,r0\\n\");"
            "return 0;}", 0, {"porta": 0x5A}),
    # sei/cli are builtins only while the name is otherwise undeclared.
    "builtin-shadowed": ("char main(){char cli = 5;char sei = 6;return cli + sei;}", 11, {}),
    # Stack balance *inside* a function. The epilogue reloads SP from BP, so a leak
    # in the body is invisible at the return -- it has to be observed mid-function,
    # which is what the two asm captures of r15 do here. 0 = balanced.
    "stack-balance": ("int [[address=0x20]] sp1;int [[address=0x22]] sp2;int g=3;"
                      "int add2(int x){return x+2;}"
                      "int main(){asm(\"stw 0x20,r15\");"
                      "for(int i=0;i<10;i=i+1){if((add2(g)-g)>500){g=g;}}"
                      "asm(\"stw 0x22,r15\");return sp1-sp2;}", 0, {}),
    # A compiled [[isr=0]] handler: vector-table placement via .org, the reactive
    # register protection, and reti. sei() arms it; crt0 leaves IE clear.
    "isr": ("int counter = 0;\n"
            "void [[isr=0]] tick() { counter = counter + 1; }\n"
            "int main() { sei(); int i = 0; for (i = 0; i < 200; i = i + 1) {} return counter; }",
            1, {"irq": {"irq_cycle": 2000, "irq_mask": 1, "irq_len": 6, "irq_period": 0}}),
    # Periodic interrupts: main only escapes the busy-wait if every pulse is
    # serviced, i.e. reti really restored IE each time. A one-shot would hang.
    "isr-periodic": ("int counter = 0;\n"
                     "void [[isr=0]] tick() { counter = counter + 1; }\n"
                     "int main() { sei(); while (counter < 5) {} return counter; }",
                     5, {"irq": {"irq_cycle": 200, "irq_mask": 1, "irq_len": 6,
                                 "irq_period": 60}}),
}


def build(irq: bool, out: str):
    defines = ["-DTEST", "-DVERBOSE", "-DSIM"]
    if irq:
        defines.append("-DIRQ_TEST")
    r = subprocess.run(["iverilog", "-o", out, "-g2012"] + defines + [CORE, TB],
                       cwd="./verilog", capture_output=True, text=True)
    if r.returncode != 0:
        raise Exception(f"iverilog failed:\n{r.stderr.strip()}")


def assemble(key: str, asm: str):
    """Assemble minc-16 source into verilog/test.hex."""
    asm_run = subprocess.run(["./target/mincasm", "-16"], input=asm,
                             capture_output=True, text=True)
    if asm_run.returncode != 0:
        raise Exception(f"[{key}] mincasm -16 failed:\n{asm_run.stderr.strip()}\n--- asm ---\n{asm}")
    with open("verilog/test.hex", "w") as f:
        f.write(asm_run.stdout)


def simulate(key: str, irq, verbose: bool):
    """Build + run the current verilog/test.hex. Returns (porta, top, sp, cycles)."""
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
    if len(lines) < 3:
        raise Exception(f"[{key}] no output from simulation")
    porta = int(lines[-3].split(":")[1].strip(), 16)
    _, top_str, sp_str = lines[-2].split(", ")
    cycles_str = lines[-1]
    cycles = int(cycles_str.split(":")[1].strip()) if ":" in cycles_str else None
    top = int(top_str.split(":")[1].strip(), 16)
    sp = int(sp_str.split(":")[1].strip(), 16)
    return porta, top, sp, cycles


def run_case(key: str, verbose: bool = False):
    fixture, expected_top, expected_sp, irq = CASES[key]
    with open(os.path.join(FIXTURES, fixture)) as f:
        asm = f.read()

    assemble(key, asm)
    _, top, sp, cycles = simulate(key, irq, verbose)

    assert top == expected_top, \
        f"[FAIL] {key}: expected TOP {expected_top:#06x}, got {top:#06x}"
    assert sp == expected_sp, \
        f"[FAIL] {key}: expected SP {expected_sp:#06x}, got {sp:#06x}"
    print(f"[OK] minc-16 asm {key:<17} => TOP: {top:#06x}, SP: {sp:#06x} ({CORE}, {cycles} cycles)")


def run_c_case(key: str, verbose: bool = False):
    """Full C round trip: mincc -16 -> mincasm -16 -> minc_16.sv."""
    code, expected_top, kwargs = C_CASES[key]
    irq = kwargs.get("irq")
    expected_porta = kwargs.get("porta")
    verbose = verbose or kwargs.get("verbose", False)

    cc = subprocess.run(["./target/mincc", "-16"], input=code,
                        capture_output=True, text=True)
    if cc.returncode != 0:
        raise Exception(f"[{key}] mincc -16 failed:\n{cc.stderr.strip()[-2000:]}")
    if verbose:
        print(cc.stdout)

    assemble(key, cc.stdout)
    porta, top, sp, cycles = simulate(key, irq, verbose)

    # Every case must leave the stack balanced: crt0 pushes main's result once.
    assert sp == 0xFFFE, f"[FAIL] {key}: expected SP 0xfffe, got {sp:#06x}\n{code}"
    assert top == expected_top, \
        f"[FAIL] {key}: expected TOP {expected_top:#06x}, got {top:#06x}\n{code}"
    if expected_porta is not None:
        assert porta == expected_porta, \
            f"[FAIL] {key}: expected PORTA {expected_porta:#04x}, got {porta:#04x}\n{code}"
    extra = f", PORTA: {porta:#04x}" if expected_porta is not None else ""
    print(f"[OK] minc-16 C   {key:<17} => TOP: {top:#06x}, SP: {sp:#06x}{extra} ({cycles} cycles)")


if __name__ == "__main__":
    if len(sys.argv) >= 2:
        key = sys.argv[1]
        if key in CASES:
            run_case(key, verbose=True)
        elif key in C_CASES:
            run_c_case(key, verbose=True)
        else:
            print(f"Unknown case '{key}'. Available: "
                  f"{', '.join(list(CASES) + list(C_CASES))}")
            sys.exit(1)
        sys.exit(0)

    for k in CASES:
        run_case(k)
    for k in C_CASES:
        run_c_case(k)
    print("\n[OK] [ALL minc-16 TESTS PASSED]")
