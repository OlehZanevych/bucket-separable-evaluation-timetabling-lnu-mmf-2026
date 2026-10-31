#!/usr/bin/env bash
# Reproduce results/diagnostics/scan-defect.csv: how often the pruning rule used before the
# correction of 17 October 2026 would have returned a worse placement than the corrected scan.
# Needs the ITC-2007 files in data-ctt/ (see data-ctt/README.md).  About 20 minutes on one core.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-diag -DCMAKE_BUILD_TYPE=Release -DTT_SCAN_DIAG=ON
cmake --build build-diag
mkdir -p results/diagnostics
tmp=$(mktemp -d)
./build-diag/experiments ctt --seeds 1 --budget 50000 --data data-ctt --results "$tmp" \
  | grep '^SCANDIAG' \
  | awk -F, 'BEGIN { print "file,arm,seed,budget,scans,worse" } { print $2 "," $3 "," $4 ",50000," $5 "," $6 }' \
  > results/diagnostics/scan-defect.csv
rm -rf "$tmp"
echo "wrote results/diagnostics/scan-defect.csv"
