# Bucket-separable evaluation and a large-neighbourhood search for large-scale university course timetabling

Code, input data and per-run records for the article *Bucket-separable evaluation and a
large-neighbourhood search for large-scale university course timetabling* (O. Zanevych,
V. Kukharskyy), submitted to *Visnyk of the Lviv University. Series Mechanics and Mathematics*, 2026.
Repository: <https://github.com/OlehZanevych/bucket-separable-evaluation-timetabling-lnu-mmf-2026>.

The article names the class of timetabling objectives that decompose over *(entity, day)* buckets,
proves that exact incremental evaluation over that class costs O(k·η·B̄²) for a candidate relocating
k classes -- independent of the number of classes whenever k, the number of entities a class touches
(η) and the affected bucket sizes (B̄) stay uniformly bounded, and each bucket's contributions can be
recomputed in time quadratic in its size -- and builds the search that spends the resulting budget
on neighbourhoods large enough to need it.

The headline measurement is the first experiment below: on the generated family, whose resources
grow in proportion to the number of classes, the number of buckets recomputed per probe is nearly
flat across a 128-fold range of instance size, while wall-clock time per probe is not, and the
difference between the two is the reason budgets in this series are counted in candidates rather
than seconds.

## Experiments

| subcommand | what it measures | article |
|---|---|---|
| `cost` | buckets recomputed and nanoseconds per candidate, against instance size | Experiment 1 |
| `evaluators` | three ways to learn a neighbour's objective, on probe streams drawn from one distribution (not one sequence) | Experiment 2 |
| `scaling` | quality at a fixed candidate budget, against instance size | Experiment 3 |
| `anytime` | quality against budget, with dispersion bands | Experiment 3 |
| `permute` | what the exact assignment sub-problem buys: occupied pool, widened pool, identity detection | Experiment 4 |
| `aiming` | where a large neighbourhood should be aimed: five victim selectors, with partition modularity | Experiment 5 |
| `instances` | writes every generated instance the experiments use, with its planted schedule, in the plain-text format | -- |
| `ctt` | Experiment 6: the twenty-one ITC-2007 curriculum-based instances, under the official UD2 objective | Experiment 6, Table 8 |
| `ctt-cost` | Experiment 6: the cost of one relocation on those instances, with and without the non-separable terms | Experiment 6 (text) |
| `ctt-selftest` | the mapping and the incremental soft terms against an independent `.ctt` validator | -- |

Each subcommand writes one CSV to `results/`, with a header row and one line per
run. Nothing is aggregated by the C++ code: aggregation is the analysis script's
job, so that the raw records survive.

The design recorded in `results/` (and run by `scripts/run_all.sh`):

```bash
./build/experiments cost       --seeds 10
./build/experiments scaling    --seeds 10 --budget 300000
./build/experiments permute    --seeds 15 --budget 200000
./build/experiments aiming     --seeds 10 --budget 200000
./build/experiments anytime    --seeds 10
./build/experiments evaluators --seeds 10
./build/experiments ctt        --seeds 3  --budget 1000000 --data data-ctt
./build/experiments ctt-cost   --budget 400000 --data data-ctt
```

`ctt` also takes `--arm bucket-local|separable-aware|fully-aware` to run one arm only, and every
subcommand takes `--seedfrom S` to run seeds S..N only, so that one design can be split over
processes or machines; the records are the same as from one process.

## Checking the implementation first

```bash
./build/experiments selftest
```

Three checks, each a few seconds. The planted schedule of every generated instance is re-scored by
the independent validator and must be feasible, so that a residual hard cost is never the
generator's fault. An instance written in the plain-text format and read back must score exactly as
the original did. And the incremental
evaluator is compared term by term with the independent validator along a random walk of
placements. Two implementations that share no code are unlikely to make the same coding error,
although both can share a misreading of the specification. `scripts/run_all.sh` runs it before anything else, together with

```bash
python3 scripts/check_stats.py
```

which checks the statistics in `scripts/ttstats.py` against their definitions rather than against
another library: the exact Wilcoxon signed-rank test is compared with an enumeration of its own null
distribution over several hundred random samples, the sign test and the effect size against values
that can be computed by hand, the chi-squared survival function against the tabulated critical
values, and Friedman's statistic against its closed form. `ttstats.py` uses the standard library
alone, so the analysis has no dependency to install and no version that can drift; the price is that
its tests have to be written, and this is them.

## Building

Requires a C++20 compiler and CMake 3.16 or newer, and Python 3.8 or newer (standard library only)
for the scripts. Nothing else: no external libraries, no package manager, no network access.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Without CMake, the same program is built by

```bash
mkdir -p build
g++ -std=c++20 -O3 -ffp-contract=off -Isrc src/main.cpp src/ctt.cpp src/core/*.cpp -pthread -o build/experiments
```

The release flags are `-O3 -ffp-contract=off`. The search compares floating-point costs, so a
compiler that fused multiplies and adds differently would change trajectories; with contraction off,
x86-64 and arm64 builds produce the same records (see below). Optional builds:
`-DTT_CHECK_SCAN=ON` (check every placement scan against an unpruned one) and `-DTT_SCAN_DIAG=ON`
(the scan-defect diagnostic).

## Reproducing the paper

```bash
scripts/check_data.sh        # the input data: shipped instances, generator, benchmark files
scripts/run_all.sh           # every experiment, into results-rerun/, then compared with results/
python3 scripts/analyse.py   # turns results/ into tables/*.tex and figures/*.dat
```

Run the scripts from the repository root. `run_all.sh` runs everything in one process; the complete
design took about 130 core-minutes on the host of the recorded run, half of it Experiment 6, and
`--seedfrom` and `--arm` (above) split a design over cores.

`run_all.sh` writes to `results-rerun/` and finishes by running `scripts/compare_results.py results
results-rerun`, which compares every record column by column and ignores only the timing columns
(`elapsed_ms`, `ns_per_candidate`); `--overwrite` writes to `results/` instead. The experiments are
deterministic given their seeds, so a rerun must reproduce every non-timing cell; see "The stored
results" below.

`run_all.sh` accepts `--quick` to run a reduced design (fewer seeds, smaller budgets) that is enough
to check that everything works end to end; it writes to `results-quick/` --- point
`analyse.py --results results-quick` at it to see the tables a reduced design produces. Some experiments fix their own budgets, a ladder that
doubles or a figure set by the quantity being traced, and a smaller `--budget` cannot shorten those;
`--maxbudget K` caps what any one run may spend, and a ladder skips the rungs above the cap rather
than repeating the highest one it is allowed, so a capped run never records a budget it did not spend.

Experiment 6 reads third-party data that is not shipped with this artefact: `data-ctt/README.md` says which
files to put there, records their checksums, and explains what is skipped if they are absent.

## Input data

Everything an experiment reads is in this directory, apart from the third-party ITC-2007 files:

- **Generated instances** (`data/`, 17 MB gzipped, 199 MB as text). Every instance the experiments
  use, with the schedule it was built around: sizes 200 to 25 600 with seeds 1-10, and seeds 11-15 at
  n = 800 and 3200 (Experiment 4). The experiments generate them in memory from (size, seed), so the
  files are not read by any experiment; they let another solver work on exactly the same instances.
  `data/SHA256SUMS` holds the checksums of the uncompressed files, and `scripts/check_data.sh`
  checks the shipped files against them and regenerates every instance with the current generator
  to check that it still produces exactly these files. The generator uses only its own random number
  generator and integer arithmetic for every decision, so its output does not depend on the standard
  library or the platform; the check has passed on x86-64 (GCC 13) and arm64 (GCC 11).
- **Benchmark instances** (`data-ctt/`): the ITC-2007 files are third-party data and must not be
  published with this artefact (see `data-ctt/README.md` for where to obtain them);
  `data-ctt/SHA256SUMS` identifies the 21 files used, `data-ctt/best-known.csv` the reference values,
  and `data-ctt/equivalence.csv` the one pair of files that is the same problem.
- **Parameters**: every design parameter is in `scripts/run_all.sh` and in the experiment drivers
  (`src/main.cpp`); the generator's parameters are the defaults of `GenOptions` in
  `src/core/generate.hpp`; the solver's are the defaults of `SearchOptions` in `src/core/search.hpp`.

`analyse.py` always reads the published `results/`. Any other directory (`--results
results-rerun`, say) is read only for files not written in the last two minutes, on the grounds that
an experiment appends to its CSV as it runs and a table built from a half-executed design looks
finished without being so. Set `TT_MIN_AGE=0` to lift that guard while iterating on the analysis.

`analyse.py` writes `tables/` and `figures/` beside the article when the directory above the
repository holds its `main.tex` (the authors' layout), and inside the repository otherwise. A fresh
clone is therefore self-contained: clone it, build it, run it, and the tables are there. Both
directories are derived and git-ignored.

Every reported experimental statistic is produced this way: the analysis script reads the raw
per-run records and writes the tables, the figure data and the numbers quoted in the prose, which
reach the text as macros in `tables/facts.tex`. Design parameters (sizes, budgets, weights), the
benchmark reference values and a few derived figures in the text are written by hand.

## The stored results

Every file in `results/` comes from one run of its complete design, made with the current code on
18 October 2026 on one host: a Linux virtual machine on an Apple arm64 computer (4 cores,
Ubuntu 22.04.5, GCC 11.4.0, `-O3 -ffp-contract=off`), four runs at a time. `results/PROVENANCE.md`
gives the details and the checksums, and `results/logs/run-2026-10-18/` holds the job list, the job
script, the progress log and the host description of that run. An earlier run of the same designs on
x86-64 (GCC 13.3.0) agrees with these records in every column except the timing columns.

## Layout

```
src/core/        the solver kernel, shared with the other applications in this series
  model.*        instances: entities, domains, the compressed tick axis
  state.*        the schedule, its (entity, day) bucket decomposition, incremental evaluation
  search.*       construction, acceptance, the seven neighbourhoods, adaptive operator selection, the escape
  validate.*     an INDEPENDENT scorer, sharing no code with state.cpp -- see below
  generate.*     instances built backwards around a hidden feasible schedule
  io.*           the plain-text instance format
  flow.hpp       Dinic's maximum flow, for the matching bounds
  hungarian.hpp  the assignment problem, for the permutation operator
  mask.hpp       128-bit tick masks
  rng.hpp        xoshiro256**, so that a seed reproduces a run exactly
  selftest.*     the checks run by `experiments selftest`
src/             this application's experiment drivers (main.cpp) and the ITC-2007 reader and
                 objective (ctt.*)
scripts/         run_all.sh, check_data.sh, compare_results.py, scan_diag.sh, analyse.py,
                 ttstats.py and check_stats.py (statistics, standard library only),
                 ctt_equivalence.py
data/            every generated instance used, gzipped, with SHA256SUMS
data-ctt/        the twenty-one ITC-2007 files are NOT shipped; SHA256SUMS, best-known.csv and
                 equivalence.csv are
results/         one CSV per experiment, one line per run; PROVENANCE.md says how each was made
  diagnostics/   the scan-defect diagnostic
  superseded/    benchmark records made before the scan correction, kept for reference
  logs/          the job list, job script, progress log and host of the recorded run
CMakeLists.txt   the build; Release is -O3 -ffp-contract=off
LICENSE          MIT
```

Everything above is tracked by git except the ITC-2007 `.ctt` files; `.gitignore` excludes only
build output, Python bytecode, the derived `tables/` and `figures/`, a reader's own
`results-rerun/` and `results-quick/`, expanded `data/*.txt` copies and editor noise.

## The Experiment 6 rerun (17 October 2026)

A review found that the placement scan (`Worker::scanBest` in `src/core/search.cpp`) compared
partial scores with the best complete score *before* adding the change in the additional objective
terms. That change can be negative on the curriculum-based instances (a lecture placed beside an
isolated one lowers compactness; a new working day lowers the minimum-working-days charge), so the
pruning could discard the true minimum. A first ad-hoc diagnostic on comp01, comp05 and comp12 put
the share of affected scans at about a tenth (`separable-aware`) and a third (`fully-aware`). The
reported figures come from the reproducible diagnostic build: `cmake -DTT_SCAN_DIAG=ON` replays the
old pruning rule beside the corrected scan on every scan and counts the scans on which the old rule
would have returned a worse placement; `ctt` then prints one `SCANDIAG` line per run. Its records,
for one run of 50 000 candidates per instance and arm, are `results/diagnostics/scan-defect.csv`
(produced by `scripts/scan_diag.sh`), and `analyse.py` turns them into the percentages in the paper.
No `bucket-local` scan is affected, since its additional change is always zero. The generated-instance experiments (1--5) have no additional terms and are
unaffected.

The scan now adds a lower bound on the change (`ExtraTerms::deltaLowerBound`: the change with no
destination room, which is exact for the two room-independent criteria and can only underestimate
the two room-dependent ones) to every partial score before comparing it. Building with
`cmake -DTT_CHECK_SCAN=ON` repeats every scan without pruning and aborts if the two disagree; a
checked run of all three arms on comp01, comp05, comp12 and comp18 passed.

All 189 benchmark runs were then repeated with the corrected code. The records stored now in
`results/ctt.csv` come from the single run of every experiment described above. The records made
before the correction are kept in `results/superseded/` (`ctt.csv` and
`ctt-loaded.csv`, two runs of the same design on a busy and an idle machine). The `bucket-local`
arm, which the defect could not affect, reproduces the superseded records exactly in cost, the four
criteria and the work counter on every run; only `elapsed_ms` differs. That is a consistency check
on the stored records, not a proof of their provenance; the release identifier and build manifest
are what record which source produced them. `ctt-cost.csv` does not use the scan.

**A second correction (17 October 2026, later).** The benchmark driver used to read the two
auxiliary counters `sep_updates` and `agg_updates` through a pointer to the worker's `SoftTerms`
object after `solve()` had destroyed it (AddressSanitizer: heap-use-after-free). The counters are
now copied into `SearchResult::extraCounters` while the worker still exists (`ExtraTerms::counters()`).
The paper does not use these two columns, and costs, criteria and work never went through that
pointer. All 189 benchmark runs were nevertheless repeated once more with the corrected driver, so
that every column of `results/ctt.csv` comes from a well-defined program. They agreed with the
previous records in every column except `elapsed_ms`. A build with `-fsanitize=address,undefined` runs the three arms, `selftest` and
`ctt-selftest` cleanly.

## Two things that make the numbers trustworthy

**An independent validator.** `src/core/validate.cpp` re-reads a schedule from scratch and counts
every penalty term and every hard filter by the most direct method the definitions allow -- sorting
and scanning, never a bitmask. It deliberately shares no code with the incremental evaluator in
`state.cpp`, so that a coding error in one is unlikely to be repeated in the other. Every returned
schedule is scored by the validator. Two kinds of number are not: the cost measurements, which
measure the incremental evaluator's own work, and the anytime trace (`anytime.csv`, Fig. 1), whose
sampled incumbent scores are the search's cached values from the incremental evaluator -- which the
self-test checks against the validator, but which are not re-scored point by point.

**Instances with a known answer.** The generator builds a valid schedule first, walking the week slot
by slot and placing each class into resources that are free at that moment and reachable in the time
available, and then reads the instance off it. So a schedule with no hard violation provably exists
for every instance, and anything the search cannot reach is a property of the search rather than of
the data. The planted schedule's own cost is reported alongside every result as a reference; it is a
feasibility witness, not a good timetable.

## Budgets

Every budget is counted in **candidate evaluations** or in **bucket recomputations**, never in
seconds. Both are deterministic and machine-independent for a single-threaded run, so a result
reproduces on other hardware. With several cooperating threads the interleaving is not deterministic,
so a multi-threaded run is not exactly reproducible; every run reported here is single-threaded. Wall-clock is recorded in
every results file alongside them, and never reported alone.

## Licence

MIT; see [LICENSE](LICENSE). The ITC-2007 instances are not part of this repository and are not
covered by it.
