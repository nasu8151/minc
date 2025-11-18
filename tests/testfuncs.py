import subprocess

def expect(command:str, expected_output:str):
    output = ""
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()

    assert output == expected_output, f"""[FAIL] Expected: "{expected_output}", but got: "{output}" """
    print(f"""[OK] "{command}" => "{output}" """)

def expect_fail(command:str):

    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()

    assert result.returncode != 0, f"""[FAIL] Expected failure but command succeeded: "{command}" """
    print(f"""[OK] "{command}" failed as expected with output: "{output}" """)

if __name__ == "__main__":
    expect("""echo -e "Hello World! \n" """, "Hello World! \n")
    expect_fail("exit 1")
