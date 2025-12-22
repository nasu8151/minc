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

def test_e2e(code:str, expected_top:int, verbose:bool=False):
    output = ""
    
    asm = subprocess.run("./target/mincc", input=code, shell=True, capture_output=True, text=True)
    if asm.returncode != 0:
        raise Exception(f"mincc failed with return code {asm.returncode}:\nStderr: {asm.stderr.strip()}")
    asm_code = asm.stdout
    inst = subprocess.run("./target/mincasm", input=asm_code, shell=True, capture_output=True, text=True)
    if inst.returncode != 0:
        raise Exception(f"mincasm failed with return code {inst.returncode}:\nStderr: {inst.stderr.strip()}")
    with open("verilog/test.hex", "w") as f:
        f.write(inst.stdout)
        f.write("7FF\n")  # insert HALT instruction
    verilog_sim = subprocess.run("cd ./verilog && iverilog -o __minc_test.out minc.sv minc_tb.sv -g2005-sv -DTEST -DVERBOSE && vvp __minc_test.out", shell=True, capture_output=True, text=True)
    if verilog_sim.returncode != 0:
        raise Exception(f"Verilog simulation failed with return code {verilog_sim.returncode}:\nStderr: {verilog_sim.stderr.strip()}")
    if verbose:
        print(verilog_sim.stdout)
    output = verilog_sim.stdout.strip().splitlines()[-1]  # Get the last line of output
    pc_str, top_str, sp_str = output.split(", ")
    top_value = int(top_str.split(": ")[1], 0)
    assert top_value == expected_top, f"""[FAIL] Expected TOP: {expected_top}, but got: {top_value} """
    print(f"""[OK] E2E test for code "{code}" => TOP: {top_value} """)

if __name__ == "__main__":
    expect("""echo "Hello World!" """, "Hello World!")
    expect_fail("cat non_existent_file.txt")
    test_e2e("1+2", 3, verbose=True)