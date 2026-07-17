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
    "localval" : ("char main(){char a=3;return a+2;}", 5, {}),
    "val-arith" : ("char main(){char a=2;char b=3;return a * b;}", 6, {}),
    "val-arith2" : ("char main(){char a=1;char b=2;char c=3;return a + b * c;}", 7, {}),
    "16bitval" : ("int main(){int a = 1234; int b = 5678; return b - a;}", 4444, {}), # 16-bit integer subtraction
    "if" : ("char main(){char hoge =3;char fuga= hoge +2;if (fuga==5) return 42;return 0;}", 42, {}),
    "ifs" : ("char main(){char hoge=2; char fuga = 3;if (hoge != 0) if (hoge+fuga > 3) return 2;else return 0;}", 2, {}),
    "for" : ("char main(){char sum=0;\nfor(char i=1;i<5;i=i+1) sum=sum+i;\nreturn sum;}", 10, {}),
    "while" : ("char main(){char i=0;\nwhile(i<3) i=i+1;\nreturn i;}", 3, {}),
    "returninwhile" : ("char main(){char i=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n}\nreturn 0;}", 100, {}),
    "globalval" : ("char gvar=10;\nchar main(){return gvar+5;}", 15, {}),
    "wtf" : ("char global_var=0;\nchar main(){while(global_var<21){for (char i=1;i<5;i=i+1) global_var=global_var+i;}\nreturn global_var;}", 30, {}),
    "betweenlocalandglobal" : ("char a=1;\nchar b=2;\nchar main(){char c=3;\nreturn a+b+c;}", 6, {}),
    "simplefunc" : ("char ret42(){return 42;}\nchar main(){return ret42();}", 42, {}),
    "fac" : ("char arg=0;char fac(){char i=arg;if (i==0) return 1;arg=i-1;return fac()*i;}char main(){arg=5;return fac();}", 120, {"verbose" : True}),
    "fib" : ("char fib(char i){if(i==0) return 0;if(i==1) return 1;char a=fib(i-1);char b=fib(i-2);return a+b;}char main(){return fib(11);}", 89, {"verbose" : False}),
    "cast" : ("int add(char a, int b){int aa = a;return aa + b;}int main(){char a = 107; int b = 1032; return add(a, b);}", 1139, {"verbose" : True}),
    "multipleargs" : ("char mac(char a,char b,char c){return a*b+c;}char main(){return mac(2,3,4);}", 10, {}),
    "scopecheck" : ("char main(){char j=0;for(char i=0;i<7;i=i+1){} char k=0; char i=i+5; return i;}", -1, {}),
    "attribute" : ("char [[address=0x04]] port_a_out;char [[address=0x05]] port_a_dir;char main(){port_a_dir = 0xFF;port_a_out=0x55; return 0;}", 0, {"porta": 0x55}),
    "break" : ("char main(){char i=0;while(1){if(i==5) break;i=i+1;}return i;}", 5, {}),
    "manyrepeations" : ("char [[address = 0x04]] b;char a;char addi(char s){a = a + s;return a;}char main(){a = 0;for(char i = 0;i < 254;i=i+1){while(addi(3) < 20){b = b;}}return 21;}", 21, {}),
    "pointer" : ("char main(){char a = 3;char *b = &a;return *b;}", 3, {}),
    "poipoi" : ("int main(){int a = 1155; int *b = &a; int **c = &b; return **c;}", 1155, {}),
    "assigninpointer" : ("char main(){char a = 3;char *b = &a;*b = 5;return a;}", 5, {}),
    "logicaland" : ("char main(){char a = 1;char b = 2;return ((a != b) && (a < b));}", 1, {}),
    "logicalor" : ("char main(){char a = 1;char b = 2;return ((a == b) || (a > b));}", 0, {}),
    "logicalnot" : ("char main(){char a = 1;char b = 2;return !b == !a;}", 1, {}),
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


if __name__ == "__main__":
    # MINCASM tests
    # tf.expect("""echo "mov r0,r1\nadd r2,r3\nsub r4,r5\nlt r6,r7\nmul r7,r8\nor r8,r9\nand r9,r10\nxor r10,r11" | ./target/mincasm""",
    #             "0001\n1023\n1845\n2067\n3878\n0489\n089A\n0CAB") # Arithmetic instructions
    # tf.expect("""echo "push r0\nsts r0\npop r2\nlds r2" | ./target/mincasm""",
    #             "7000\n7800\n7420\n7C20") # Stack and load/store instructions
    # tf.expect("""echo "jz 10,r0\ncalr 20\njr -2\nret\nhalt" | ./target/mincasm""",
    #             "D00A\nE104\nFFFE\n7410\nFFFF") # Jump and calr instructions
    # tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    # tf.expect_fail("""echo "mvi r0,256" | ./target/mincasm""") # Out of range immediate
    # # MINCASM label tests (one-pass backpatch)
    # # Forward reference: label after use
    # tf.expect("""echo "jz L1,r0\nmvi r0,1\nL1: ret" | ./target/mincasm""",
    #             "D001\nC001\n7410")
    # # Backward reference: label before use
    # tf.expect("""echo "L0: mvi r0,1\njz L0,r0\nret" | ./target/mincasm""",
    #             "C001\nDF0E\n7410")
    # # calr to label
    # tf.expect("""echo "calr MAIN\nFUNC: ret\nMAIN: calr FUNC\nhalt" | ./target/mincasm""",
    #             "E001\n7410\nEFFE\nFFFF")
    # Undefined label should fail
    tf.expect_fail("""echo "jz NO_SUCH_LABEL,r0" | ./target/mincasm""")
    # MINCC tests
    tf.expect_fail("""echo "char main(){1+}" | ./target/mincc""") # Incomplete expression
    tf.expect_fail("""echo "char main(){a+1=5;}" | ./target/mincc""") # Invalid assignment
    tf.expect_fail("""echo "char main(){i>=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n\nreturn 0;}" | target/mincc """) # Missing closing brace
    tf.expect_fail("""echo "char main(){char a = 3;int b = &a;return *b;}" | ./target/mincc""")

    # E2E tests
    if (len(sys.argv) == 1):
        run_e2e_tests()
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

    if (len(sys.argv) >= 2):
        run_e2e_tests(key=sys.argv[1])

    print()
    print("[OK] [ALL TESTS PASSED]")
