import testfuncs as tf
import test as t

CORES = ["minc_h.sv", "minc_p2.sv", "minc_p5.sv"]


def run_correctness():
    """Run the full E2E suite against each pipelined core (binary compatibility check)."""
    for core in ("minc_p2.sv", "minc_p5.sv"):
        print(f"\n=== {core}: correctness ===")
        t.run_e2e_tests(core=core)


def run_comparison():
    """Run each E2E case against every core and print a cycle-count comparison table."""
    print("\n=== Cycle count comparison ===")
    rows = []
    for code, expected_top, kwargs in t.E2E_CASES:
        cycles = {}
        for core in CORES:
            cycles[core] = tf.test_e2e(code, expected_top, core=core, **kwargs)
        rows.append((code, cycles))

    header = f"{'code':42} " + " ".join(f"{c:>12}" for c in CORES) + "     p2 speedup   p5 speedup"
    print(header)
    print("-" * len(header))
    totals = {c: 0 for c in CORES}
    for code, cycles in rows:
        for c in CORES:
            totals[c] += cycles[c] or 0
        h, p2, p5 = cycles["minc_h.sv"], cycles["minc_p2.sv"], cycles["minc_p5.sv"]
        speedup_p2 = f"{h / p2:5.2f}x" if p2 else "?"
        speedup_p5 = f"{h / p5:5.2f}x" if p5 else "?"
        print(f"{code[:42]:42} {h:>12} {p2:>12} {p5:>12}     {speedup_p2:>10}   {speedup_p5:>10}")
    print("-" * len(header))
    h, p2, p5 = totals["minc_h.sv"], totals["minc_p2.sv"], totals["minc_p5.sv"]
    print(f"{'TOTAL':42} {h:>12} {p2:>12} {p5:>12}     {h/p2:9.2f}x   {h/p5:9.2f}x")


if __name__ == "__main__":
    run_correctness()
    run_comparison()
    print()
    print("[OK] [ALL PIPELINE TESTS PASSED]")
