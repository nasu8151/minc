#!/usr/bin/env bash
set -euo pipefail

# This test suite validates current behavior of mincc and mincasm
# - mincc: prints `ld <code>` to stdout when given one argument; exits non-zero on wrong usage
# - mincasm: assembles `ld <imm>` and `add <imm>` into 3-hex-digit opcodes and writes to outfile

ROOT_DIR="$(cd "$(dirname "$0")"/.. && pwd)"
BINDIR="$ROOT_DIR/target"
TESTDIR="$BINDIR/test"
MINCC="$BINDIR/mincc"
MINCASM="$BINDIR/mincasm"

mkdir -p "$TESTDIR"

echo "[tests] Using binaries: $MINCC, $MINCASM"

# Helper for expecting failure
expect_fail() {
  set +e
  ("$@") >/dev/null 2>&1
  local rc=$?
  set -e
  if [ $rc -eq 0 ]; then
    echo "[FAIL] Expected failure but succeeded: $*"
    exit 1
  else
    echo "[ OK ] Expected failure confirmed: $* (rc=$rc)"
  fi
}

# 1) Integration: mincc output piped to mincasm should assemble to 02a
echo "[test 1] mincc|mincasm integration (ld 0x2a -> 02a)"
"$MINCC" 0x2a > "$TESTDIR/in1.asm"
"$MINCASM" "$TESTDIR/in1.asm" "$TESTDIR/out1.hex"
echo "02a" > "$TESTDIR/exp1.hex"
diff -u "$TESTDIR/exp1.hex" "$TESTDIR/out1.hex"
echo "[ OK ] test 1 passed"

# 2) mincasm add immediate: add 5 -> 105
echo "[test 2] mincasm add immediate (add 5 -> 105)"
echo "add 5" > "$TESTDIR/in2.asm"
"$MINCASM" "$TESTDIR/in2.asm" "$TESTDIR/out2.hex"
echo "105" > "$TESTDIR/exp2.hex"
diff -u "$TESTDIR/exp2.hex" "$TESTDIR/out2.hex"
echo "[ OK ] test 2 passed"

# 3) mincasm operand out of range (> 0xFF) should fail
echo "[test 3] mincasm operand out of range (ld 0x1ff) expects failure"
echo "ld 0x1ff" > "$TESTDIR/in3.asm"
expect_fail "$MINCASM" "$TESTDIR/in3.asm" "$TESTDIR/out3.hex"

echo "[ OK ] test 3 passed"

# 4) mincasm unsupported instruction should fail
echo "[test 4] mincasm unsupported instruction (sub 1) expects failure"
echo "sub 1" > "$TESTDIR/in4.asm"
expect_fail "$MINCASM" "$TESTDIR/in4.asm" "$TESTDIR/out4.hex"

echo "[ OK ] test 4 passed"

# 5) mincc wrong usage (no args) should fail
echo "[test 5] mincc wrong usage (no args) expects failure"
expect_fail "$MINCC"

echo "[ OK ] test 5 passed"

# 6) mincc : load immediate and add immediate integration
echo "[test 6] mincc|mincasm integration (ld 0x10; add 0x20 -> 010, 120)"
"$MINCC" 0x10+0x20 > "$TESTDIR/in6.asm"
"$MINCASM" "$TESTDIR/in6.asm" "$TESTDIR/out6.hex"
echo -e "010\n120" > "$TESTDIR/exp6.hex"
diff -u "$TESTDIR/exp6.hex" "$TESTDIR/out6.hex"

echo "[ OK ] test 6 passed"

# 7) mincc : tokenizer should handle spaces
echo "[test 7] mincc tokenizer handles spaces (  1  +     2  -> ld 1 ; add 2)"
"$MINCC" "  1   +    2  " > "$TESTDIR/in7.asm"
"$MINCASM" "$TESTDIR/in7.asm" "$TESTDIR/out7.hex"
echo -e "001\n102" > "$TESTDIR/exp7.hex"
diff -u "$TESTDIR/exp7.hex" "$TESTDIR/out7.hex"

echo "[ OK ] test 7 passed"

echo "\nAll tests passed."