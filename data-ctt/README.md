# The standard curriculum-based instances

Experiment 6 runs on the twenty-one instances of the curriculum-based track of the second
International Timetabling Competition (ITC-2007). They are third-party data and are **not shipped
with this artefact**; put `comp01.ctt` … `comp21.ctt` in this directory and the experiment will find
them. Without them `run_all.sh` skips Experiment 6 and every other experiment runs as before. (The
authors' working copy holds the files here; a public copy of this artefact must not include them.)

Sources: the competition's own pages (Queen's University Belfast, https://www.eeecs.qub.ac.uk/itc2007/),
which distribute the original `.ctt` files, and OPTHUB (https://opthub.uniud.it), the repository
maintained by the authors of the 2023 survey, which distributes the instances in the extended `.ectt`
format together with published results. The reader here takes `.ctt`. Whatever source you take them
from, check them against `SHA256SUMS` in this directory, which records the twenty-one files this
paper's numbers were produced from:

```bash
sha256sum -c SHA256SUMS
```

The reader also validates each file against its own declared counts — courses, rooms, curricula and
unavailability constraints — and refuses to run on one that does not match, because a truncated
instance parses cleanly and yields a plausible, wrong answer.

`best-known.csv` holds the UD2 bounds used as the scale column of Table `tab:ctt`: the best-known
upper bound, the best-known lower bound, and a flag for the seventeen instances where the two are
equal and the value is therefore proved optimal. Those are not results of this work and are not
verified here; the file's header says which collection they come from and what was checked against
what. Four instances remain open -- comp03, comp05, comp12 and comp15 -- and are the ones on which a
later collection is most likely to differ.

## Two files, one problem

`comp03` and `comp15` are the same problem under renamed course, teacher, room and curriculum
identifiers, and every one of their records agrees. The classes are recorded in
`equivalence.csv` (file, representative, SHA-256), which ships with the results so that the analysis
does not need the third-party files; `python3 scripts/ctt_equivalence.py data-ctt --check` verifies it
against the files when they are present. The 21-file table keeps both, as the benchmark does;
`analyse.py` makes its instance-level inference over the 20 distinct problems, keeping `comp03`, and
stops with an error if the manifest is missing, incomplete, or disagrees with the files present.

## What is run

```bash
./build/experiments ctt-selftest --data data-ctt     # mapping and evaluator against the validator
./build/experiments ctt --seeds 3 --budget 1000000 --data data-ctt
./build/experiments ctt-cost --budget 400000 --data data-ctt
```

`ctt-selftest` is the one to run first. For every instance it walks a random sequence of placements
and requires the incremental soft terms to agree with a validator that reads the `.ctt` file and
shares no code with them, and requires the incremental maintenance to be exactly invertible. It
does not compare the two hard counts value for value and should not: the validator counts each
violated constraint once, while the bucket evaluator counts clash incidences, and one pair of
lectures of the same course collides on the course entity, on the teacher's, and on every curriculum
the course belongs to. What the test requires is that the two vanish together, which is what
feasibility means here.
