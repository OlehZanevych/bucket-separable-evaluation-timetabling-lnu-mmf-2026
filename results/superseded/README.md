# Superseded records

`ctt.csv` and `ctt-loaded.csv` are the Experiment 6 records made before the placement-scan
correction of 17 October 2026 (see "The Experiment 6 rerun" in `../../README.md`). They are two
complete runs of the same design, one on a busy and one on an idle machine; cost and work agree on
all 189 runs and only `elapsed_ms` differs. Their `separable-aware` and `fully-aware` rows were
produced by a scan that could miss the minimising placement, so the paper no longer uses them. Their
`bucket-local` rows equal those of `../ctt.csv` in every column except `elapsed_ms`.
