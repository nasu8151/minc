import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo "ld 1\nld 2\nadd\nsub\nmul" | ./target/mincasm""", 
                "001\n002\n100\n200\n300") # All valid instructions
    tf.expect_fail("""echo foo | ./target/mincasm""") # Invalid instruction
    tf.expect_fail("""echo "ld 256" | ./target/mincasm""") # Out of range immediate
    # MINCC tests
    tf.expect("""echo 1+2 | ./target/mincc""",
                "ld 1\nld 2\nadd") # Simple addition
    tf.expect("""echo "1  + 2" | ./target/mincc""",
                "ld 1\nld 2\nadd") # Addition with spaces
    tf.expect("""echo "1+2*3" | ./target/mincc""",
                "ld 1\nld 2\nld 3\nmul\nadd") # Addition and multiplication
    tf.expect("""echo "(1+2)*3" | ./target/mincc""",
                "ld 1\nld 2\nadd\nld 3\nmul") # Parentheses
    tf.expect("""echo "(10-3)*(10+3)" | ./target/mincc""",
                "ld 10\nld 3\nsub\nld 10\nld 3\nadd\nmul") # Complex expression
    tf.expect_fail("""echo "1+" | ./target/mincc""") # Incomplete expression
    tf.expect("""echo "-5+(+3)" | ./target/mincc""",
                "ld 0\nld 5\nsub\nld 0\nld 3\nadd\nadd") # Unary minus and plus

    print("[OK] [ALL TESTS PASSED]")
