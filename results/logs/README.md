# Logs of the recorded run

`run-2026-10-18/` documents the run that produced every file in `results/` (see `../PROVENANCE.md`).

| File | What it is |
|---|---|
| `jobs.txt` | the 276 jobs the designs were split into, one per line |
| `job.sh` | the script that ran one job, as used; it built each job's command line from the design in `scripts/run_all.sh` |
| `jobs.log` | the progress output of every job, in the order of `jobs.txt`, with the time it finished |
| `host.txt` | the machine, operating system and compiler |

**How the jobs map to the design:**

- **`gen E S`**: experiment `E` (cost, evaluators, scaling, anytime, permute, aiming) for seed `S`
  alone (`--seeds S --seedfrom S`), at the budget of `scripts/run_all.sh`.
- **`ctt I A S`**: Experiment 6 on instance `compI`, arm `A`, seed `S`. Each instance was in a
  directory of its own (`dI/`).
- **`cost`**: the Experiment 6 cost probe (`ctt-cost`), not Experiment 1.
- **`diag I`**: the scan-defect diagnostic on `compI`, built with `-DTT_SCAN_DIAG=ON`. Its
  `SCANDIAG` lines are the rows of `../diagnostics/scan-defect.csv`.

**Running and merging:**

- Four jobs ran at a time (`xargs -P4 -L1 ./job.sh < jobs.txt`), each writing to its own directory.
- A job that was interrupted was rerun from scratch until its `done` marker was written.
- The per-job CSVs were then concatenated, under one header, in the order a single process writes
  them.

**Paths:** `job.sh` refers to paths on that machine: `$HOME/a2b/R` was the working directory, and
`exp`/`exp_diag` were the two builds. To repeat the run, use `scripts/run_all.sh`, or split it the
same way with `--seedfrom` and `--arm`.
