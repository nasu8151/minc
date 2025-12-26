import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo "mov r0,r1\nadd r2,r3\nsub r4,r5\nlt r6,r7\nmul r7,r8" | ./target/mincasm""", 
                "0001\n0123\n0245\n0367\n0478") # Arithmetic instructions
    tf.expect("""echo "push r0\nsts r1\npop r2\nlds r3" | ./target/mincasm""",
                "0800\n0901\n0A20\n0B30") # Stack and load/store instructions
    tf.expect("""echo "jz 10,r0\njnz 15,r1\ncall 20\nret" | ./target/mincasm""",
                "40A0\n60F1\n5140\n0C00") # Jump and call instructions
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
    tf.expect("""echo "call FUNC\nret\nFUNC: ret" | ./target/mincasm""",
                "5020\n0C00\n0C00")
    # Undefined label should fail
    tf.expect_fail("""echo "jz NO_SUCH_LABEL,r0" | ./target/mincasm""")
    # MINCC tests
    tf.expect_fail("""echo "1+" | ./target/mincc""") # Incomplete expression
    tf.expect_fail("""echo "a+1=5;" | ./target/mincc""") # Invalid assignment

    # E2E tests
    tf.test_e2e("1+2;", 3)
    tf.test_e2e("10-3;", 7)
    tf.test_e2e("2*3;", 6)
    tf.test_e2e("(1+2)*3;", 9)
    tf.test_e2e("-3+5;", 2)
    tf.test_e2e("-(2+3)*4;", -20)
    tf.test_e2e("+5+(+3);", 8)
    tf.test_e2e("1+1==2;", 1)
    tf.test_e2e("1+1==3;", 0)
    tf.test_e2e("2*2!=5;", 1)
    tf.test_e2e("2*2!=4;", 0)
    tf.test_e2e("3+2<6;", 1)
    tf.test_e2e("3+3<=6;", 1)
    tf.test_e2e("5>2+2;", 1)
    tf.test_e2e("2+2>=4;", 1)
    tf.test_e2e("a=3;a+2;", 5)
    tf.test_e2e("a=2;b=3;a*b;", 6)
    tf.test_e2e("hoge=4;fuga=5;hoge+fuga;", 9)


    print()
    print("[OK] [ALL TESTS PASSED]")
