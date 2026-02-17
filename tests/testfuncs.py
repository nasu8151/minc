import subprocess

def expect(command:str, expected_output:str):
    escaped_expected_output = expected_output.replace("\n", "\\n").replace("\r", "\\r")
    output = ""
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()
    error  = result.stderr.strip()

    assert result.returncode == 0, f"""[FAIL] Command failed with return code {result.returncode}: "{command}" \nStderr: "{error}" """
    assert output == expected_output, f"""[FAIL] Expected: "{expected_output}", but got: "{output}" """
    print(f"""[OK] "{command}" => "{output}" """)

def expect_fail(command:str):

    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    output = result.stdout.strip()
    error  = result.stderr.strip()

    assert result.returncode != 0, f"""[FAIL] Expected failure but command succeeded: "{command}" """
    print(f"""[OK] "{command}" failed as expected with output: "{output}"\nand stderr: "{error}" """)

def test_e2e(code:str, expected_top:int, verbose:bool=False, porta = None):
    output = ""
    
    asm = subprocess.run("./target/mincc", input=code, shell=True, capture_output=True, text=True)
    if asm.returncode != 0:
        raise Exception(f"mincc failed with return code {asm.returncode}:\nStderr:\n{asm.stderr}")
    asm_code = asm.stdout
    inst = subprocess.run("./target/mincasm", input=asm_code, shell=True, capture_output=True, text=True)
    if inst.returncode != 0:
        raise Exception(f"mincasm failed with return code {inst.returncode}:\nStderr:\n{inst.stderr}")
    with open("verilog/test.hex", "w") as f:
        f.write(inst.stdout)
    synsesis = subprocess.run(["iverilog", "-o", "__minc_test.out", "minc.sv", "minc_tb.sv", "-g2005-sv", "-DTEST", "-DVERBOSE", "-DSIM"], cwd="./verilog", capture_output=True, text=True)
    if synsesis.returncode != 0:
        raise Exception(f"Verilog synthesis failed with return code {synsesis.returncode}:\nStderr: {synsesis.stderr.strip()}")
    verilog_sim = subprocess.run(["vvp", "./__minc_test.out"], cwd="./verilog", capture_output=True, text=True)
    if verilog_sim.returncode != 0:
        raise Exception(f"Verilog simulation failed with return code {verilog_sim.returncode}:\nStderr: {verilog_sim.stderr.strip()}")
    if verbose:
        print(verilog_sim.stdout)
    
    output = verilog_sim.stdout.strip()  # Get the last line of output
    lines = output.splitlines()
    if not lines:
        raise Exception("No output from simulation")
    porta_str = lines[-2].strip().split(", ")
    pc_str, top_str, sp_str = lines[-1].split(", ")
    if top_str.split(": ")[1] == 'xx' and expected_top == -1:
        print(f"""[OK] E2E test for code "{code}" => TOP: xx as expected""")
        return
    top_value = int(top_str.split(": ")[1], 16)
    if porta is not None:
        port_a_value = int(porta_str[0].split(": ")[1], 16)
        assert port_a_value == (porta & 0xff), f"""[FAIL] Expected PORTA: {porta}, but got: {port_a_value} """
        print(f"""[OK] E2E test for code "{code}" => PORTA: {port_a_value} """)
    assert top_value == (expected_top & 0xff), f"""[FAIL] Expected TOP: {expected_top}, but got: {top_value} """
    print(f"""[OK] E2E test for code "{code}" => TOP: {top_value} """)

if __name__ == "__main__":
    expect("""echo "Hello World!" """, "Hello World!")
    expect_fail("cat non_existent_file.txt")
    test_e2e("1+2", 3, verbose=True)