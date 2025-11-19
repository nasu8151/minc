import subprocess

def expect(command:str, expected_output:str):
    escaped_expected_output = expected_output.replace("\n", "\\n").replace("\r", "\\r")
    output = ""
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()

    escaped_command = command.replace("\n", "\\n").replace("\r", "\\r")
    escaped_output = output.replace("\n", "\\n").replace("\r", "\\r")

    assert output == expected_output, f"""[FAIL] Expected: "{escaped_expected_output}", but got: "{escaped_output}" """
    print(f"""[OK] "{escaped_command}" => "{escaped_output}" """)

def expect_fail(command:str):

    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()

    assert result.returncode != 0, f"""[FAIL] Expected failure but command succeeded: "{command}" """
    escaped_command = command.replace("\n", "\\n").replace("\r", "\\r")
    escaped_output = output.replace("\n", "\\n").replace("\r", "\\r")
    print(f"""[OK] "{escaped_command}" failed as expected with output: "{escaped_output}" """)

if __name__ == "__main__":
    expect("""echo "Hello World! \n" """, "Hello World! \n")
    expect_fail("exit 1")
