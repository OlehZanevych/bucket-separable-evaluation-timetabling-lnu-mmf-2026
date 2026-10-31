#!/usr/bin/env python3
"""Find curriculum-based instances that are the same problem under renamed identifiers.

Two .ctt files are reported equivalent when they agree after every course, teacher, room and
curriculum name is replaced by its position in the file.  Equality of that normal form is a
certificate of equivalence; it is not a general isomorphism test, and two equivalent files whose
records are listed in a different order would not be detected.  ITC-2007 contains one such pair
(comp03 and comp15), and an instance-level test that counts both treats one problem as two
independent observations.

The classes are recorded in data-ctt/equivalence.csv (file, problem, sha256), which analyse.py reads,
so that the analysis does not depend on the third-party files being present.  With the files present:

    python3 scripts/ctt_equivalence.py data-ctt            # print the classes
    python3 scripts/ctt_equivalence.py data-ctt --check    # verify equivalence.csv against the files
    python3 scripts/ctt_equivalence.py data-ctt --write    # rewrite equivalence.csv
"""
import hashlib
import glob
import os
import sys


def normal_form(path):
    lines = [l.strip() for l in open(path, encoding="utf-8")]
    head, sections, cur = {}, {}, None
    for l in lines:
        if not l or l == "END.":
            continue
        if l.endswith(":") and l[:-1].isupper():
            cur = l[:-1]
            sections[cur] = []
            continue
        if cur is None:
            k, _, v = l.partition(":")
            if k != "Name":
                head[k.strip()] = v.strip()
        else:
            sections[cur].append(l.split())
    teacher, course, room, curr = {}, {}, {}, {}
    courses = []
    for c, t, lec, mind, stud in sections.get("COURSES", []):
        course[c] = len(course)
        teacher.setdefault(t, len(teacher))
        courses.append((teacher[t], int(lec), int(mind), int(stud)))
    rooms = []
    for r, cap in sections.get("ROOMS", []):
        room[r] = len(room)
        rooms.append(int(cap))
    curricula = []
    for row in sections.get("CURRICULA", []):
        curr[row[0]] = len(curr)
        curricula.append(tuple(sorted(course[c] for c in row[2:])))
    unav = sorted((course[c], int(d), int(p))
                  for c, d, p in sections.get("UNAVAILABILITY_CONSTRAINTS", []))
    return (tuple(sorted(head.items())), tuple(courses), tuple(rooms), tuple(curricula), tuple(unav))


def classes(data_dir):
    """Lists of instance stems (e.g. 'comp03') that are the same problem, in file order."""
    by_form = {}
    for path in sorted(glob.glob(os.path.join(data_dir, "comp*.ctt"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        by_form.setdefault(normal_form(path), []).append(stem)
    return list(by_form.values())


def manifest_rows(data_dir):
    """(file, problem, sha256) for every comp*.ctt, problem being the first file of its class."""
    rows = []
    for c in classes(data_dir):
        for stem in c:
            with open(os.path.join(data_dir, stem + ".ctt"), "rb") as f:
                rows.append((stem, c[0], hashlib.sha256(f.read()).hexdigest()))
    return sorted(rows)


def read_manifest(data_dir):
    """{file: problem} from equivalence.csv; raises if the manifest is missing or malformed."""
    path = os.path.join(data_dir, "equivalence.csv")
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("file,"):
                continue
            stem, problem, _sha = line.split(",")
            if stem in out:
                raise ValueError(f"{path}: {stem} listed twice")
            out[stem] = problem
    for stem, problem in out.items():
        if out.get(problem) != problem:
            raise ValueError(f"{path}: {stem} maps to {problem}, which is not its own representative")
    return out


def write_manifest(data_dir):
    path = os.path.join(data_dir, "equivalence.csv")
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Written by scripts/ctt_equivalence.py --write.  problem: the representative file of\n")
        f.write("# the file's equivalence class (identical after identifier normalisation).\n")
        f.write("file,problem,sha256\n")
        for r in manifest_rows(data_dir):
            f.write(",".join(r) + "\n")
    return path


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    d = args[0] if args else "data-ctt"
    cl = classes(d)
    if not cl:
        sys.exit(f"no comp*.ctt under {d}")
    if "--write" in sys.argv:
        print("wrote", write_manifest(d))
    elif "--check" in sys.argv:
        want = {(f, p, h) for f, p, h in manifest_rows(d)}
        got = set()
        with open(os.path.join(d, "equivalence.csv"), encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and not line.startswith("file,"):
                    got.add(tuple(line.split(",")))
        if want != got:
            sys.exit("equivalence.csv does not match the files: " + str(sorted(want ^ got)))
        print("equivalence.csv matches the files")
    for c in cl:
        if len(c) > 1:
            print("equivalent:", " = ".join(c))
    print(f"{sum(len(c) for c in cl)} files, {len(cl)} distinct problems")
