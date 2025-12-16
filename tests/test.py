import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo "mov r0,r1\nadd r2,r3\nsub r4,r5\nmul r6,r7" | ./target/mincasm""", 
                "0001\n0123\n0245\n0367") # Arithmetic instructions
    tf.expect("""echo "push r0\nlds r1\npop r2\nsts r3" | ./target/mincasm""",
                "0400\n0411\n0520\n0531") # Stack and load/store instructions
    tf.expect("""echo "jz 10,r0\ncall 20\nret" | ./target/mincasm""",
                "40A0\n5140\n0502") # Jump and call instructions
    tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    tf.expect_fail("""echo "mvi r0,256" | ./target/mincasm""") # Out of range immediate
    # MINCC tests
    tf.expect("""echo 1+2 | ./target/mincc""",
                "mvi r0,1\npush r0\nmvi r0,2\npush r0\npop r1\npop r0\nadd r0,r1\npush r0") # Simple addition
    tf.expect("""echo "1  + 2" | ./target/mincc""",
                "mvi r0,1\npush r0\nmvi r0,2\npush r0\npop r1\npop r0\nadd r0,r1\npush r0") # Addition with spaces
    tf.expect("""echo "1+2*3" | ./target/mincc""",
                "mvi r0,1\npush r0\nmvi r0,2\npush r0\nmvi r0,3\npush r0\npop r1\npop r0\nmul r0,r1\npush r0\npop r1\npop r0\nadd r0,r1\npush r0") # Addition and multiplication
    tf.expect("""echo "(1+2)*3" | ./target/mincc""",
                "mvi r0,1\npush r0\nmvi r0,2\npush r0\npop r1\npop r0\nadd r0,r1\npush r0\nmvi r0,3\npush r0\npop r1\npop r0\nmul r0,r1\npush r0") # Parentheses
    tf.expect("""echo "(10-3)*(10+3)" | ./target/mincc""",
                "mvi r0,10\npush r0\nmvi r0,3\npush r0\npop r1\npop r0\nsub r0,r1\npush r0\nmvi r0,10\npush r0\nmvi r0,3\npush r0\npop r1\npop r0\nadd r0,r1\npush r0\npop r1\npop r0\nmul r0,r1\npush r0") # Complex expression
    tf.expect_fail("""echo "1+" | ./target/mincc""") # Incomplete expression
    tf.expect("""echo "-5+(+3)" | ./target/mincc""",
                "mvi r0,0\npush r0\nmvi r0,5\npush r0\npop r1\npop r0\nsub r0,r1\npush r0\nmvi r0,0\npush r0\nmvi r0,3\npush r0\npop r1\npop r0\nadd r0,r1\npush r0\npop r1\npop r0\nadd r0,r1\npush r0") # Unary minus and plus
    tf.test_e2e("1+2", 3)
    tf.test_e2e("10-3", 7)
    tf.test_e2e("2*3", 6)
    tf.test_e2e("(1+2)*3", 9)
    tf.test_e2e("-3+5", 2)

    print()
    print("[OK] [ALL TESTS PASSED]")
