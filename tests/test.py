import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo "push 1\npush 2\nadd\nsub\nmul" | ./target/mincasm""", 
                "001\n002\n400\n500\n600") # All valid instructions
    tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    tf.expect_fail("""echo "push 256" | ./target/mincasm""") # Out of range immediate
    # MINCC tests
    tf.expect("""echo 1+2 | ./target/mincc""",
                "push 1\npush 2\nadd") # Simple addition
    tf.expect("""echo "1  + 2" | ./target/mincc""",
                "push 1\npush 2\nadd") # Addition with spaces
    tf.expect("""echo "1+2*3" | ./target/mincc""",
                "push 1\npush 2\npush 3\nmul\nadd") # Addition and multiplication
    tf.expect("""echo "(1+2)*3" | ./target/mincc""",
                "push 1\npush 2\nadd\npush 3\nmul") # Parentheses
    tf.expect("""echo "(10-3)*(10+3)" | ./target/mincc""",
                "push 10\npush 3\nsub\npush 10\npush 3\nadd\nmul") # Complex expression
    tf.expect_fail("""echo "1+" | ./target/mincc""") # Incomplete expression
    tf.expect("""echo "-5+(+3)" | ./target/mincc""",
                "push 0\npush 5\nsub\npush 0\npush 3\nadd\nadd") # Unary minus and plus
    tf.test_e2e("1+2", 3)
    tf.test_e2e("10-3", 7)
    tf.test_e2e("2*3", 6)
    tf.test_e2e("(1+2)*3", 9)
    tf.test_e2e("-3+5", 2)

    print()
    print("[OK] [ALL TESTS PASSED]")
