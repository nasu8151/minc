import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo "mov r0,r1\nadd r2,r3\nsub r4,r5\nlt r6,r7\nmul r7,r8\nor r8,r9\nand r9,r10\nxor r10,r11" | ./target/mincasm""", 
                "0001\n1023\n1845\n2067\n3878\n0489\n089A\n0CAB") # Arithmetic instructions
    tf.expect("""echo "push r0\nsts r0\npop r2\nlds r2" | ./target/mincasm""",
                "7000\n7800\n7420\n7C20") # Stack and load/store instructions
    tf.expect("""echo "jz 10,r0\ncalr 20\njr -2\nret\nhalt" | ./target/mincasm""",
                "D00A\nE104\nFFFE\n7410\nFFFF") # Jump and calr instructions
    tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    tf.expect_fail("""echo "mvi r0,256" | ./target/mincasm""") # Out of range immediate
    # MINCASM label tests (one-pass backpatch)
    # Forward reference: label after use
    tf.expect("""echo "jz L1,r0\nmvi r0,1\nL1: ret" | ./target/mincasm""",
                "D001\nC001\n7410")
    # Backward reference: label before use
    tf.expect("""echo "L0: mvi r0,1\njz L0,r0\nret" | ./target/mincasm""",
                "C001\nDF0E\n7410")
    # calr to label
    tf.expect("""echo "calr MAIN\nFUNC: ret\nMAIN: calr FUNC\nhalt" | ./target/mincasm""",
                "E001\n7410\nEFFE\nFFFF")
    # Undefined label should fail
    tf.expect_fail("""echo "jz NO_SUCH_LABEL,r0" | ./target/mincasm""")
    # MINCC tests
    tf.expect_fail("""echo "char main(){1+}" | ./target/mincc""") # Incomplete expression
    tf.expect_fail("""echo "char main(){a+1=5;}" | ./target/mincc""") # Invalid assignment
    tf.expect_fail("""echo "char main(){i>=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n\nreturn 0;}" | target/mincc """) # Missing closing brace

    # E2E tests
    tf.test_e2e("char main(){return 1+2;}", 3)
    tf.test_e2e("char main(){return 0b1101;}", 13)
    tf.test_e2e("char main(){return 10-3;}", 7)
    tf.test_e2e("char main(){return 2*3;}", 6)
    tf.test_e2e("char main(){return (1+2)*3;}", 9)
    tf.test_e2e("char main(){return -3+5;}", 2)
    tf.test_e2e("char main(){return -(2+3)*4;}", -20)
    tf.test_e2e("char main(){return +5+(+3);}", 8)
    tf.test_e2e("char main(){return 1+1==2;}", 1)
    tf.test_e2e("char main(){return 1+1==3;}", 0)
    tf.test_e2e("char main(){return 2*2!=5;}", 1)
    tf.test_e2e("char main(){return (-2)*(-2)!=4;}", 0)
    tf.test_e2e("char main(){return (3+2<6)+(3+2<5);}", 1)
    tf.test_e2e("char main(){return (3+3<=6)+(3+3<=5);}", 1)
    tf.test_e2e("char main(){return (5>2+2)+(4>2+2);}", 1)
    tf.test_e2e("char main(){return (2+2>=4)+(2+2>=5);}", 1)
    tf.test_e2e("char main(){return ~(((0b11111111 & 0b11000011) | 0b00001111) ^ 0b00111100);}", 0b00001100, verbose=True)
    tf.test_e2e("char main(){char a=3;return a+2;}", 5)
    tf.test_e2e("char main(){char a=2;char b=3;return a * b;}", 6)
    tf.test_e2e("char main(){char a=1;char b=2;char c=3;return a + b * c;}", 7)
    tf.test_e2e("char main(){char hoge =3;char fuga= hoge +2;if (fuga==5) return 42;return 0;}", 42)
    tf.test_e2e("char main(){char hoge=2; char fuga = 3;if (hoge != 0) if (hoge+fuga > 3) return 2;else return 0;}", 2)
    tf.test_e2e("char main(){char sum=0;\nfor(char i=1;i<5;i=i+1) sum=sum+i;\nreturn sum;}", 10)
    tf.test_e2e("char main(){char i=0;\nwhile(i<3) i=i+1;\nreturn i;}", 3)
    tf.test_e2e("char main(){char i=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n}\nreturn 0;}", 100)
    tf.test_e2e("char gvar=10;\nchar main(){return gvar+5;}", 15)
    tf.test_e2e("char global_var=0;\nchar main(){while(global_var<21){for (char i=1;i<5;i=i+1) global_var=global_var+i;}\nreturn global_var;}", 30)
    tf.test_e2e("char a=1;\nchar b=2;\nchar main(){char c=3;\nreturn a+b+c;}", 6)
    tf.test_e2e("char ret42(){return 42;}\nchar main(){return ret42();}", 42)
    tf.test_e2e("char arg=0;char fac(){char i=arg;\nif (i==0) return 1;\narg=i-1;\nreturn fac()*i;}char main(){arg=5;\nreturn fac();}", 120)
    tf.test_e2e("char fib(char i){if(i==0) return 0;\nif(i==1) return 1;\nchar a=fib(i-1);\nchar b=fib(i-2);\nreturn a+b;}\nchar main(){return fib(11);}" , 89)
    tf.test_e2e("char mac(char a,char b,char c){return a*b+c;}char main(){return mac(2,3,4);}", 10)
    tf.test_e2e("char main(){char j=0;for(char i=0;i<7;i=i+1){} char k=0; char i=i+5; return i;}", -1)
    tf.test_e2e("char [[address=0x00]] port_a_out;char [[address=0x01]] port_a_dir;char main(){port_a_dir = 0xFF;port_a_out=0x55; return 0;}", 0, porta=0x55)
    tf.test_e2e("char main(){char i=0;while(1){if(i==5) break;i=i+1;}return i;}", 5)
    tf.test_e2e("char [[address = 0x00]] b;char a;char addi(char s){a = a + s;return a;}void main(){a = 0;for(char i = 0;i < 254;i=i+1){while(addi(3) < 20){b = b;}}return 21;}", 21)
    tf.test_e2e("char main(){char a = 3;char b = &a;return *b;}", 3)
    tf.test_e2e("char main(){char a = 3;char *b = &a;*b = 5;return a;}", 5)


    print()
    print("[OK] [ALL TESTS PASSED]")
