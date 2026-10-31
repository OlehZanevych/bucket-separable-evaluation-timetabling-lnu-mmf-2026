#!/usr/bin/env python3
"""Compare two results directories record by record, ignoring timing.

Every experiment is deterministic given its seed except for wall-clock time, so a rerun of the
recorded design must reproduce every other column exactly.  This compares each CSV present in both
directories, keyed by the columns that identify a run, and reports the cells that differ.

Usage:  python3 scripts/compare_results.py results results-rerun
Exit status 1 if any deterministic cell differs or a run is missing from the second directory.
"""
import csv
import os
import sys

# Columns that identify a record, and columns that are timings and may differ.
KEYS = {
    "cost.csv": ("n", "seed"),
    "evaluators.csv": ("n", "seed", "evaluator"),
    "scaling.csv": ("n", "seed"),
    "anytime.csv": ("n", "seed", "candidates"),
    "permute.csv": ("n", "seed", "arm"),
    "aiming.csv": ("n", "seed", "selector"),
    "ctt.csv": ("file", "arm", "seed"),
    "ctt-cost.csv": ("file", "arm"),
    os.path.join("diagnostics", "scan-defect.csv"): ("file", "arm", "seed"),
}
TIMING = {"elapsed_ms", "ns_per_candidate"}


def load(path, key):
    with open(path, newline="") as f:
        return {tuple(r[k] for k in key): r for r in csv.DictReader(f)}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    a, b = sys.argv[1], sys.argv[2]
    bad = 0
    compared = 0
    for name, key in KEYS.items():
        pa, pb = os.path.join(a, name), os.path.join(b, name)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            continue
        ra, rb = load(pa, key), load(pb, key)
        missing = [k for k in ra if k not in rb]
        diffs = [(k, c, ra[k][c], rb[k][c]) for k in ra if k in rb
                 for c in ra[k] if c not in TIMING and ra[k][c] != rb[k].get(c)]
        compared += 1
        status = "identical" if not missing and not diffs else "DIFFERENT"
        print(f"{name:28s} {len(ra):4d} records, {len(rb):4d} in rerun: {status}"
              + (f"; {len(missing)} missing" if missing else "")
              + (f"; {len(diffs)} cells differ" if diffs else ""))
        for d in diffs[:5]:
            print("    ", d)
        bad += bool(missing or diffs)
    if not compared:
        sys.exit("no results file present in both directories")
    print("all compared records identical outside timing" if not bad else "records differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
