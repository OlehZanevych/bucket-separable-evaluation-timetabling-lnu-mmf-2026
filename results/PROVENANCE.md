# Provenance of the records in this directory

Every record here comes from one run of every experiment design, made with the final source on one
host. Each experiment is deterministic given its seed except for wall-clock time, so a rerun of a
design must reproduce every column except `elapsed_ms` and `ns_per_candidate`
(`scripts/run_all.sh` reruns everything into `results-rerun/` and compares the two through
`scripts/compare_results.py`).

## The records

| File | Experiment | Design (see `scripts/run_all.sh`) | Rows |
|---|---|---|---:|
| `cost.csv` | 1 | `cost --seeds 10` | 80 |
| `evaluators.csv` | 2 | `evaluators --seeds 10` | 180 |
| `scaling.csv` | 3 | `scaling --seeds 10 --budget 300000` | 70 |
| `anytime.csv` | 3 | `anytime --seeds 10` | 980 |
| `permute.csv` | 4 | `permute --seeds 15 --budget 200000` | 120 |
| `aiming.csv` | 5 | `aiming --seeds 10 --budget 200000` | 100 |
| `ctt.csv` | 6 | `ctt --seeds 3 --budget 1000000 --data data-ctt` | 189 |
| `ctt-cost.csv` | 6 | `ctt-cost --budget 400000 --data data-ctt` | 42 |
| `diagnostics/scan-defect.csv` | 6 (diagnostic) | `scripts/scan_diag.sh` | 63 |

## The run

- **Date:** 18 October 2026, 14:19–14:52 UTC.
- **Host:** Linux virtual machine on an Apple arm64 computer, 4 cores, Ubuntu 22.04.5.
- **Build:** g++ 11.4.0, Release (`-O3 -ffp-contract=off`), C++20, from the source in `../src` as
  it stands.
- **Concurrency:** four single-threaded processes at a time, one per core. The timing columns were
  therefore measured with the other cores busy, and are compared only within an experiment.
- **Splitting:** each design was split over processes by seed (`--seedfrom`), and the benchmark
  also by instance and arm (`--arm`). The pieces were merged in the order one process writes them.
  Splitting does not change any record, since every record depends only on its own seed.
- **Binary:** rebuilding this repository's source on the same host with the command in
  `../README.md` (`g++ -std=c++20 -O3 -ffp-contract=off …`) gives a binary byte-identical to the one
  that made the run (MD5 `f4caa872c5287b10a9c9bc30540df4c0`).
- **Logs:** `logs/run-2026-10-18/` holds the job list, the job script, the progress log of every
  job and the host description (`logs/README.md`).

## Portability check

Before this run the same designs had been run on an x86-64 machine (Intel Xeon, Ubuntu 24.04.5,
GCC 13.3.0) with the source as of 17 October 2026. Those records agree with the ones stored here in
every column except the two timing columns. The solver, the generator and the experiments did not
change between the two sources. The integer and floating-point results therefore do not depend on
the processor architecture or the compiler version tried.

`superseded/` holds the benchmark records made before the scan correction described in
`../README.md`. The article does not use them.

## Checksums (SHA-256)

```
0eba4b5e309637be7dc5a9eded832c89d713b4b34af662d50c764886047dc467  aiming.csv
6d09295175a5c49c3c9f9ab00dc6738f5b8730cc4e28c31c93f344db3a73a276  anytime.csv
5c2354fb99f31bdbe83be6ab0e689134a0941b57d8a29ef957958ccb344f9c9e  cost.csv
959a80b34068dc9799c448b18a97e0f74f958cc772aefdefdccd6017d2e75563  ctt-cost.csv
d9aad96cb9d33b1f7fb4be459c43876476e0da0057007c2c09e46711f3613deb  ctt.csv
58fd6c8ee6c4a5568f20e596cde73eaf1c3fe023f637a13b56acb68cd1b9c6c8  evaluators.csv
79bf4a203c718432038cb18098e06544c68c73fe4896fb5bf7ee6769117e0864  permute.csv
d7a43deb99c7d42d873e15051246c232515b075f4838ffc8a1f51bb3c7e53b58  scaling.csv
79762cc2b2550172a6d7b56e064585cd4386e530481f72a4db76d26273ad33bb  diagnostics/scan-defect.csv
```
