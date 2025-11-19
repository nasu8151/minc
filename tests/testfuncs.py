import subprocess

def expect(command:str, expected_output:str):
    escaped_expected_output = expected_output.replace("\n", "\\n").replace("\r", "\\r")
    output = ""
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()
    error  = result.stderr.strip()

    escaped_command = command.replace("\n", "\\n").replace("\r", "\\r")
    escaped_output = output.replace("\n", "\\n").replace("\r", "\\r")
    escaped_error = error.replace("\n", "\\n").replace("\r", "\\r")

    assert result.returncode == 0, f"""[FAIL] Command failed with return code {result.returncode}: "{escaped_command}" \nStderr: "{escaped_error}" """
    assert output == expected_output, f"""[FAIL] Expected: "{escaped_expected_output}", but got: "{escaped_output}" """
    print(f"""[OK] "{escaped_command}" => "{escaped_output}" """)

def expect_fail(command:str):

    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()
    error  = result.stderr.strip()

    escaped_command = command.replace("\n", "\\n").replace("\r", "\\r")
    escaped_output = output.replace("\n", "\\n").replace("\r", "\\r")
    escaped_error = error.replace("\n", "\\n").replace("\r", "\\r")

    assert result.returncode != 0, f"""[FAIL] Expected failure but command succeeded: "{command}" """
    print(f"""[OK] "{escaped_command}" failed as expected with output: "{escaped_output}"\nand stderr: "{escaped_error}" """)

if __name__ == "__main__":
    expect("""echo "Hello World!" """, "Hello World!")
    expect_fail("cat non_existent_file.txt")
