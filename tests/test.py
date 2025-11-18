import testfuncs as tf

if __name__ == "__main__":
    # MINCASM tests
    tf.expect("""echo -e "ld 1\nld 2\nadd\nsub\nmul" | target/mincasm""", 
                "001\n002\n100\n200\n300")
