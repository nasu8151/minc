import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo "mov r0,r1\nadd r2,r3\nsub r4,r5\nlt r6,r7\nmul r7,r8" | ./target/mincasm""", 
                "0001\n0123\n0245\n0367\n0478") # Arithmetic instructions
    tf.expect("""echo "push r0\nsts r1\npop r2\nlds r3" | ./target/mincasm""",
                "0800\n0901\n0A20\n0B30") # Stack and load/store instructions
    tf.expect("""echo "jz 10,r0\njnz 15,r1\ncall 20\nret\nhalt" | ./target/mincasm""",
                "40A0\n60F1\n5140\n0C00\n7FFF") # Jump and call instructions
    tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    tf.expect_fail("""echo "mvi r0,256" | ./target/mincasm""") # Out of range immediate
    # MINCASM label tests (one-pass backpatch)
    # Forward reference: label after use
    tf.expect("""echo "jz L1,r0\nmvi r0,1\nL1: ret" | ./target/mincasm""",
                "4020\n1010\n0C00")
    # Backward reference: label before use
    tf.expect("""echo "L0: mvi r0,1\njz L0,r0\nret" | ./target/mincasm""",
                "1010\n4000\n0C00")
    # jnz to label
    tf.expect("""echo "L0: mvi r0,1\njnz L0,r1\nret" | ./target/mincasm""",
                "1010\n6001\n0C00")
    # Call to label
    tf.expect("""echo "call FUNC\nhalt\nFUNC: ret" | ./target/mincasm""",
                "5020\n7FFF\n0C00")
    # Undefined label should fail
    tf.expect_fail("""echo "jz NO_SUCH_LABEL,r0" | ./target/mincasm""")
    # MINCC tests
    tf.expect_fail("""echo "main(){1+}" | ./target/mincc""") # Incomplete expression
    tf.expect_fail("""echo "main(){a+1=5;}" | ./target/mincc""") # Invalid assignment
    tf.expect_fail("""echo "main(){i>=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n\nreturn 0;}" | target/mincc """) # Missing closing brace

    # E2E tests
    tf.test_e2e("main(){return 1+2;}", 3)
    tf.test_e2e("main(){return 10-3;}", 7)
    tf.test_e2e("main(){return 2*3;}", 6)
    tf.test_e2e("main(){return (1+2)*3;}", 9)
    tf.test_e2e("main(){return -3+5;}", 2)
    tf.test_e2e("main(){return -(2+3)*4;}", -20)
    tf.test_e2e("main(){return +5+(+3);}", 8)
    tf.test_e2e("main(){return 1+1==2;}", 1)
    tf.test_e2e("main(){return 1+1==3;}", 0)
    tf.test_e2e("main(){return 2*2!=5;}", 1)
    tf.test_e2e("main(){return (-2)*(-2)!=4;}", 0)
    tf.test_e2e("main(){return (3+2<6)+(3+2<5);}", 1)
    tf.test_e2e("main(){return (3+3<=6)+(3+3<=5);}", 1)
    tf.test_e2e("main(){return (5>2+2)+(4>2+2);}", 1)
    tf.test_e2e("main(){return (2+2>=4)+(2+2>=5);}", 1)
    tf.test_e2e("main(){a=3;return a+2;}", 5)
    tf.test_e2e("main(){a=2;b=3;return a*b;}", 6)
    tf.test_e2e("main(){hoge=4;fuga=5;return hoge+fuga;}", 9)
    tf.test_e2e("main(){a =1;\nb = 2;\nc  = 3;\nreturn a + b* c;}", 7)
    tf.test_e2e("main(){hoge =3;\nfuga= hoge +2;\nif (fuga==5) return 42;\nreturn 0;}", 42)
    tf.test_e2e("main(){hoge=2; fuga = 3;\nif (hoge != 0) if (hoge+fuga > 3) return 2;\nelse return 0;}", 2)
    tf.test_e2e("main(){sum=0;\nfor(i=1;i<5;i=i+1) sum=sum+i;\nreturn sum;}", 10)
    tf.test_e2e("main(){i=0;\nwhile(i<3) i=i+1;\nreturn i;}", 3)
    tf.test_e2e("main(){i=0;\nwhile(i<10) {\n i=i+1;\n if (i==5) {\nreturn 20*i;\n}\n}\nreturn 0;}", 100)
    tf.test_e2e("gvar=10;\nmain(){return gvar+5;}", 15)
    tf.test_e2e("global_var=0;\nmain(){while(global_var<21){for (i=1;i<5;i=i+1) global_var=global_var+i;}\nreturn global_var;}", 30)
    tf.test_e2e("a=1;\nb=2;\nmain(){c=3;\nreturn a+b+c;}", 6)
    tf.test_e2e("ret42(){return 42;}\nmain(){return ret42();}", 42)
    tf.test_e2e("arg=0;fac(){i=arg;\nif (i==0) return 1;\narg=i-1;\nreturn fac()*i;}main(){arg=5;\nreturn fac();}", 120)
    tf.test_e2e("fib(i){if(i==0) return 0;\nif(i==1) return 1;\na=fib(i-1);\nb=fib(i-2);\nreturn a+b;}\nmain(){return fib(11);}" , 89)
    tf.test_e2e("mac(a,b,c){return a*b+c;}main(){return mac(2,3,4);}", 10)
    tf.test_e2e("main(){j=0;for(i=0;i<7;i=i+1){} k=0; i=i+5; return i;}", -1)


    print()
    print("[OK] [ALL TESTS PASSED]")
