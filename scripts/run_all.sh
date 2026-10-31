#!/usr/bin/env bash
# Run every experiment of this article, in the order their conclusions depend on
# one another.  --quick runs a reduced design that finishes in minutes.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -x build/experiments ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
fi
mkdir -p results data

# Before any experiment: check the generator, the instance file format and the two independent
# evaluators against each other.  A failure here would make every number below meaningless.
./build/experiments selftest
python3 scripts/check_stats.py
# The input data: the shipped instances, the generator that reproduces them, the benchmark files.
scripts/check_data.sh

QUICK=0
OVERWRITE=0
for arg in "$@"; do
  [ "$arg" = "--quick" ] && QUICK=1
  [ "$arg" = "--overwrite" ] && OVERWRITE=1
done

# Neither a replication nor a smoke test writes over the records the paper was built from unless
# --overwrite is given.  A full run writes to results-rerun/ and is then compared column by column
# with results/ (scripts/compare_results.py); a quick run is a smoke test with a reduced design and
# writes to results-quick/.  Point analyse.py at either with --results to see its tables.
RES=results-rerun
if [ $OVERWRITE -eq 1 ]; then RES=results; fi
if [ $QUICK -eq 1 ]; then RES=results-quick; fi
mkdir -p "$RES"
run() { ./build/experiments "$@" --results "$RES"; }

# Experiment 6 needs the twenty-one ITC-2007 curriculum-based instances, which are third-party data and are
# not shipped here; data-ctt/README.md says where to put them and how to check them.  Without them
# the rest of the study is unaffected, so this skips rather than fails.
run_ctt() {
  if ! compgen -G "data-ctt/comp*.ctt" > /dev/null; then
    echo "skipping Experiment 6: no comp*.ctt in data-ctt/ (see data-ctt/README.md)"
    return 0
  fi
  ./build/experiments ctt-selftest --data data-ctt || return 1
  ./build/experiments ctt "$@" --data data-ctt --results "$RES"
  ./build/experiments ctt-cost --budget 400000 --data data-ctt --results "$RES"
}

if [ $QUICK -eq 1 ]; then
  run cost --seeds 3 --maxbudget 20000
  run scaling --seeds 3 --budget 30000
  run permute --seeds 3 --budget 30000
  run aiming --seeds 3 --budget 30000
  run anytime --seeds 3 --maxbudget 20000
  run evaluators --seeds 3
  run_ctt --seeds 1 --budget 30000
else
  run cost       --seeds 10
  run scaling    --seeds 10 --budget 300000
  # Experiments 4 and 5 were recorded at 200 000 applied candidates per run.
  run permute    --seeds 15 --budget 200000
  run aiming     --seeds 10 --budget 200000
  run anytime    --seeds 10
  run evaluators --seeds 10
  run_ctt        --seeds 3  --budget 1000000
fi

echo
echo "results written to $RES/"
if [ $QUICK -eq 0 ] && [ "$RES" != results ]; then
  python3 scripts/compare_results.py results "$RES"
fi
echo "tables: python3 scripts/analyse.py --results $RES"
