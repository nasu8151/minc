#!/usr/bin/env python3

import testfuncs as tf
from typing import Optional
import sys

# Each entry: (code, expected_top, kwargs for tf.test_e2e)
E2E_CASES = {
    "basic" : ("char main(){return 1+2;}", 3, {}),
    "binary" : ("char main(){return 0b1101;}", 13, {}),
    "subtract" : ("char main(){return 10-3;}", 7, {}),
    "multiply" : ("char main(){return 2*3;}", 6, {}),
    "brace" : ("char main(){return (1+2)*3;}", 9, {}),
    "negative" : ("char main(){return -3+5;}", 2, {}),
    "negativebrace" : ("char main(){return -(2+3)*4;}", (-20 & 0xFF), {}), # currently char is unsigned
    "positive" : ("char main(){return +5+(+3);}", 8, {}),
    "equalto" : ("char main(){return 1+1==2;}", 1, {}),
    "!equalto" : ("char main(){return 1+1==3;}", 0, {}),
    "nonequalto" : ("char main(){return 2*2!=5;}", 1, {}),
    "!nonequalto" : ("char main(){return (-2)*(-2)!=4;}", 0, {}),
    "lessthan" : ("char main(){return (3+2<6)+(3+2<5);}", 1, {}),
    "lessthanequal" : ("char main(){return (3+3<=6)+(3+3<=5);}", 1, {}),
    "greaterthan" : ("char main(){return (5>2+2)+(4>2+2);}", 1, {}),
    "greaterthanequal" : ("char main(){return (2+2>=4)+(2+2>=5);}", 1, {}),
    "bitmasks" : ("char main(){return ~(((0b11111111 & 0b11000011) | 0b00001111) ^ 0b00111100);}", 0b00001100, {}),
    "ikisugi" : ("int main(){return 1919 + 4545;}", 6464, {}), # 数値に特に深い意味はない
    "lessthan16" : ("int main(){return (5555<5556)+(5555<5555);}", 1, {"verbose": True}),
    "lessthanequal16" : ("int main(){return (666<=666)+(666<=665);}", 1, {}),
    "greaterthan16" : ("int main(){return (4445>4444)+(4444>4444);}", 1, {}),
    "greaterthanequal16" : ("int main(){return (4444>=4444)+(4444>=4445);}", 1, {}),
    "localval" : ("char main(){char a=3;return a+2;}", 5, {}),
    "val-arith" : ("char main(){char a=2;char b=3;return a * b;}", 6, {}),
    "val-arith2" : ("char main(){char a=1;char b=2;char c=3;return a + b * c;}", 7, {}),
    "16bitval" : ("int main(){int a = 1234; int b = 5678; return b - a;}", 4444, {}), # 16-bit integer subtraction
    "if" : ("char main(){char hoge =3;char fuga= hoge +2;if (fuga==5) return 42;return 0;}", 42, {}),
    "ifs" : ("char main(){char hoge=2; char fuga = 3;if (hoge != 0) if (hoge+fuga > 3) return 2;else return 0;}", 2, {}),
    "if16" : ("int main(){int hoge = 3000;int fuga = hoge + 2000;if (hoge == 3000 && fuga == 5000) return 42;return 0;}", 42, {}),
    "ifs16" : ("int main(){int hoge = 3000;int fuga = hoge + 2000;if (hoge == 3001 && fuga == 5000) return 42; else if (hoge == 3000 && fuga == 5000) return 46;return 0;}", 46, {}),
    "for" : ("char main(){char sum=0;\nfor(char i=1;i<5;i=i+1) sum=sum+i;\nreturn sum;}", 10, {}),
    "while" : ("char main(){char i=0;\nwhile(i<3) i=i+1;\nreturn i;}", 3, {}),
    "returninwhile" : ("char main(){char i=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n}\nreturn 0;}", 100, {}),
    "globalval" : ("char gvar=10;\nchar main(){return gvar+5;}", 15, {}),
    "wtf" : ("char global_var=0;\nchar main(){while(global_var<21){for (char i=1;i<5;i=i+1) global_var=global_var+i;}\nreturn global_var;}", 30, {}),
    "betweenlocalandglobal" : ("char a=1;\nchar b=2;\nchar main(){char c=3;\nreturn a+b+c;}", 6, {}),
    "simplefunc" : ("char ret42(){return 42;}\nchar main(){return ret42();}", 42, {}),
    "fac" : ("char arg=0;char fac(){char i=arg;if (i==0) return 1;arg=i-1;return fac()*i;}char main(){arg=5;return fac();}", 120, {}),
    "fib" : ("char fib(char i){if(i==0) return 0;if(i==1) return 1;char a=fib(i-1);char b=fib(i-2);return a+b;}char main(){return fib(11);}", 89, {}),
    # "superfib" : ("int fib(int i){if(i==0) return 0;if(i==1) return 1;int a=fib(i-1);int b=fib(i-2);return a+b;}int main(){int i = 20;return fib(i);}", 6765, {}),
    "cast" : ("int add(char a, int b){int aa = a;return aa + b;}int main(){char a = 107; int b = 1032; return add(a, b);}", 1139, {}),
    "multipleargs" : ("char mac(char a,char b,char c){return a*b+c;}char main(){return mac(2,3,4);}", 10, {}),
    "scopecheck" : ("char main(){char j=0;for(char i=0;i<7;i=i+1){} char k=0; char i; i=i+5; return i;}", -1, {}),
    "attribute" : ("char [[address=0x04]] port_a_out;char [[address=0x05]] port_a_dir;char main(){port_a_dir = 0xFF;port_a_out=0x55; return 0;}", 0, {"porta": 0x55}),
    "break" : ("char main(){char i=0;while(1){if(i==5) break;i=i+1;}return i;}", 5, {}),
    "manyrepeations" : ("char [[address = 0x04]] b;char a;char addi(char s){a = a + s;return a;}char main(){a = 0;for(char i = 0;i < 254;i=i+1){while(addi(3) < 20){b = b;}}return 21;}", 21, {}),
    "pointer" : ("char main(){char a = 3;char *b = &a;return *b;}", 3, {}),
    "poipoi" : ("int main(){int a = 1155; int *b = &a; int **c = &b; return **c;}", 1155, {}),
    "assigninpointer" : ("char main(){char a = 3;char *b = &a;*b = 5;return a;}", 5, {}),
    # `*p = e` must evaluate e exactly once. Codegen used to emit the right-hand
    # side twice (once up front, once inside the ND_DEREF arm), so a call with a
    # side effect ran twice and every such store leaked a regstack slot.
    # bump() called once => v=1, n=1 => 11; called twice => v=2, n=2 => 22.
    "derefassign-once" : ("char n=0;char bump(){n=n+1;return n;}char main(){char v=0;char *p=&v;*p=bump();return v*10+n;}", 11, {}),
    # Same check for a 2-byte store (no `*`: gen_i16 has no ND_MUL).
    # once => v=301, n=301 => 602; twice => v=302, n=302 => 604.
    "derefassign-once16" : ("int n=300;int bump(){n=n+1;return n;}int main(){int v=0;int *p=&v;*p=bump();return v+n;}", 602, {}),
    "logicaland" : ("char main(){char a = 1;char b = 2;return ((a != b) && (a < b));}", 1, {}),
    "logicalor" : ("char main(){char a = 1;char b = 2;return ((a == b) || (a > b));}", 0, {}),
    "logicalnot" : ("char main(){char a = 1;char b = 2;return (!b == 0) && (!a == 0);}", 1, {}),
    # Inline assembly. Emitted verbatim, so these reach PORTA (0x04) directly
    # rather than through any generated store.
    "asm-single" : ("char [[address=0x05]] d;char main(){d=0xFF;asm(\"mvi r0,3\\nstm 4,r0\");return 0;}", 0, {"porta": 3}),
    "asm-concat" : ("char [[address=0x05]] d;char main(){d=0xFF;asm(\"mvi r0,0x5A\\n\" \"stm 4,r0\\n\");return 0;}", 0, {"porta": 0x5A}),
    # asm blocks may carry mincasm labels/comments and are not reordered around
    # surrounding C statements.
    "asm-label" : ("char [[address=0x05]] d;char main(){d=0xFF;asm(\"mvi r0,0\\n__asmloop:\\nadd r0,r0\\nmvi r1,8\\n\" \"or r0,r1 ; comment\\nstm 4,r0\\n\");return 0;}", 0, {"porta": 8}),
    # `sei`/`cli` are builtins only while the name is otherwise undeclared, so a
    # user's own variable of that name still wins.
    "builtin-shadowed" : ("char main(){char cli = 5;char sei = 6;return cli + sei;}", 11, {}),
    # The callee-saved register saves must run exactly once per call, so they
    # belong in the prologue. Emitting them at first use put them wherever that
    # use landed -- here the first use of r6/r7 is inside the loop, so the push
    # ran 10 times against a single pop and leaked 2 bytes per iteration. SP is
    # memory-mapped at 0x0000, so the leak is read straight back out of C:
    # 0 when balanced, 20 with the old lazy scheme. Note the epilogue restores
    # SP from the frame pointer, which is why this has to be observed *inside*
    # the function rather than through the harness's end-of-run SP check.
    "regsave-hoisted" : ("char [[address=0x00]] sp_lo;int g=3;int add2(int x){return x+2;}char main(){char before=sp_lo;for(char i=0;i<10;i=i+1){if((add2(g)-g)>500){g=g;}}char after=sp_lo;return before-after;}", 0, {}),
}


def run_e2e_tests(core: str = "minc_h.sv", key: Optional[str] = None):
    """Run every E2E case against the given core file. Returns total cycle count."""
    total_cycles = 0
    if key:
        code, expected_top, kwargs = E2E_CASES[key]
        print(code, key)
        cycles = tf.test_e2e(code, expected_top, key, core=core, **kwargs)
        return cycles

    for title, slice in E2E_CASES.items():
        code, expected_top, kwargs = slice
        cycles = tf.test_e2e(code, expected_top, title, core=core, **kwargs)
        if cycles is not None:
            total_cycles += cycles
    return total_cycles

def run_irq_tests():
    # minc_h.sv interrupt hardware tests (hand-assembled fixture, no mincc
    # involved -- see tests/fixtures/irq_vector.asm). Each case fires the
    # interrupt mid-loop via a different irq_mask and checks that: the correct
    # ISR (by vector priority) ran, execution resumed at the exact interrupted
    # instruction, and RETI restored PSR (carry+IE) from the auto-saved shadow
    # even though the ISR deliberately clobbers PSR before returning.
    IRQ_FIXTURE = "tests/fixtures/irq_vector.asm"
    tf.test_irq(IRQ_FIXTURE, irq_cycle=40, irq_mask=1, expected_top=0xA003, title="irq-vector0")
    tf.test_irq(IRQ_FIXTURE, irq_cycle=40, irq_mask=4, expected_top=0xA203, title="irq-vector2")
    tf.test_irq(IRQ_FIXTURE, irq_cycle=40, irq_mask=8, expected_top=0xA303, title="irq-vector3")
    tf.test_irq(IRQ_FIXTURE, irq_cycle=40, irq_mask=0b0101, expected_top=0xA003, title="irq-priority-0-over-2")
    tf.test_irq(IRQ_FIXTURE, irq_cycle=40, irq_mask=0b1010, expected_top=0xA103, title="irq-priority-1-over-3")

    # mincc-compiled [[isr=N]] end-to-end: proves the full mincc -> mincasm ->
    # minc_h.sv pipeline for a *compiled* ISR (vector-table placement, reactive
    # register save/restore, RETI), not just the hand-assembled fixture above.
    ISR_C_SRC = """
char [[address=0x02]] psr;
char [[address=0x04]] port_a_out;
char [[address=0x05]] port_a_dir;
int counter = 0;

void [[isr=0]] tick() {
counter = counter + 1;
port_a_out = 0xA5;
}

char main() {
port_a_dir = 0xFF;
psr = 2;
char i = 0;
for (i = 0; i < 200; i = i + 1) {}
return counter;
}
"""
    tf.test_irq_e2e(ISR_C_SRC, irq_cycle=4000, irq_mask=1, expected_top=1, porta=0xA5, title="isr-compiled-e2e")

    # Periodic interrupts: irq_period makes minc_tb.sv re-pulse irq_in every
    # N cycles instead of firing once. main busy-waits until the ISR's
    # counter reaches TARGET, which only passes if every periodic pulse is
    # actually serviced (IE correctly restored by RETI after each one) --
    # a single one-shot interrupt would hang until the test timeout.
    ISR_PERIODIC_C_SRC = """
char [[address=0x02]] psr;
int counter = 0;

void [[isr=0]] tick() {
counter = counter + 1;
}

int main() {
psr = 2;
int cur = counter;
while ((counter - cur) < 5) {
}
cur = counter;
while ((counter - cur) < 5) {
}
return counter;
}
"""
    tf.test_irq_e2e(ISR_PERIODIC_C_SRC, irq_cycle=200, irq_period=500, irq_len=6,
                        irq_mask=1, expected_top=10, title="isr-periodic-e2e")

    # sei()/cli() builtins, checked in both directions against a *periodic*
    # irq_in so neither can pass by accident:
    #   - cli() broken (IE left set by the preceding sei()) -> a pulse lands
    #     during the long for-loop and main returns the 0xEE sentinel.
    #   - sei() broken (IE never set) -> the while below never observes counter
    #     move and the simulation runs to its timeout instead of returning 42.
    # `base` is sampled after cli() rather than compared against 0, so a pulse
    # slipping through the few instructions between sei() and cli() can't make
    # this flaky. Nothing here touches [[address=0x02]] psr: interrupts are
    # masked out of reset, so sei() is the only thing that can arm them.
    ISR_SEI_CLI_C_SRC = """
int counter = 0;

void [[isr=0]] tick() {
counter = counter + 1;
}

char main() {
sei();
cli();
int base = counter;
for (char i = 0; i < 200; i = i + 1) {}
if (counter != base) return 0xEE;
sei();
while (counter == base) {}
return 42;
}
"""
    tf.test_irq_e2e(ISR_SEI_CLI_C_SRC, irq_cycle=400, irq_period=800, irq_len=6,
                        irq_mask=1, expected_top=42, title="isr-sei-cli-e2e")


if __name__ == "__main__":
    # MINCASM tests
    # tf.expect("./target/mincasm", """mov r0,r1\nadd r2,r3\nsub r4,r5\nlt r6,r7\nmul r7,r8\nor r8,r9\nand r9,r10\nxor r10,r11""",
    #             "0001\n1023\n1845\n2067\n3878\n0489\n089A\n0CAB") # Arithmetic instructions
    # tf.expect("./target/mincasm", """push r0\nsts r0\npop r2\nlds r2""",
    #             "7000\n7800\n7420\n7C20") # Stack and load/store instructions
    # tf.expect("./target/mincasm", """jz 10,r0\ncalr 20\njr -2\nret\nhalt""",
    #             "D00A\nE104\nFFFE\n7410\nFFFF") # Jump and calr instructions
    # tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    # tf.expect_fail("./target/mincasm", """mvi r0,256""") # Out of range immediate
    # # MINCASM label tests (one-pass backpatch)
    # # Forward reference: label after use
    # tf.expect("./target/mincasm", """jz L1,r0\nmvi r0,1\nL1: ret""",
    #             "D001\nC001\n7410")
    # # Backward reference: label before use
    # tf.expect("./target/mincasm", """L0: mvi r0,1\njz L0,r0\nret""",
    #             "C001\nDF0E\n7410")
    # # calr to label
    # tf.expect("./target/mincasm", """calr MAIN\nFUNC: ret\nMAIN: calr FUNC\nhalt""",
    #             "E001\n7410\nEFFE\nFFFF")
    # Undefined label should fail
    tf.expect_fail("./target/mincasm", "jz NO_SUCH_LABEL,r0")
    # .org directive: forward org pads the gap with zero words, and labels
    # defined after an .org land at the new address.
    tf.expect("""./target/mincasm""",
                "jr MAIN\n.org 3\nMAIN:\n    halt\n", "30002\n00000\n00000\n3FFFF")
    # .org directive: an explicit vector-table style layout (jump to MAIN at
    # address 0, jump to ISR0 at address 1, code resuming at address 5).
    tf.expect("./target/mincasm", ".org 0\njr MAIN\n.org 1\njr ISR0\n.org 5\nMAIN:\n    halt\nISR0:\n    reti\n",
                "30004\n30004\n00000\n00000\n00000\n3FFFF\n1E000")
    # .org directive: missing/out-of-range address should fail
    tf.expect_fail("./target/mincasm", ".org")
    tf.expect_fail("./target/mincasm", ".org 70000")
    # MINCC tests
    tf.expect_fail("./target/mincc", """char main(){1+}""") # Incomplete expression
    tf.expect_fail("./target/mincc", """char main(){a+1=5;}""") # Invalid assignment
    tf.expect_fail("./target/mincc", """char main(){int i=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n\nreturn 0;}""") # Missing closing brace
    tf.expect_fail("./target/mincc", """char main(){char a = 3;int b = &a;return *b;}""")
    # [[isr]]/[[isr=N]] validation
    tf.expect_fail("./target/mincc", """void [[isr=0]] tick(){return 1;}""") # return-with-value in ISR
    tf.expect_fail("./target/mincc", """void [[isr=0]] tick(char x){}""") # ISR with a parameter
    tf.expect_fail("./target/mincc", """void [[isr=5]] tick(){}""") # vector out of range
    tf.expect_fail("./target/mincc", """void [[isr=0]] a(){}void [[isr=0]] b(){}char main(){return 0;}""") # duplicate vector claim
    tf.expect_fail("./target/mincc", """void [[isr=0]] tick(){}char main(){tick();return 0;}""") # calling an ISR directly
    # asm() / builtin validation
    tf.expect_fail("./target/mincc", """char main(){asm(1);}""") # asm operand must be a string literal
    tf.expect_fail("./target/mincc", """char main(){asm("mvi r0,1\\n";}""") # missing closing paren
    tf.expect_fail("./target/mincc", """char main(){asm("mvi r0,1);}""") # unterminated string literal
    tf.expect_fail("./target/mincc", """char main(){asm("mvi r0,1\\p");}""") # unknown escape sequence
    tf.expect_fail("./target/mincc", """char main(){char x = sei();return x;}""") # sei() has no value
    tf.expect_fail("./target/mincc", """char main(){char x = asm("ret");return x;}""") # asm is a statement, not an expression

    # E2E tests
    if (len(sys.argv) == 1):
        run_e2e_tests()
        run_irq_tests()

    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "irq"):
            run_irq_tests()
        else:
            run_e2e_tests(key=sys.argv[1])

    print()
    print("[OK] [ALL TESTS PASSED]")
