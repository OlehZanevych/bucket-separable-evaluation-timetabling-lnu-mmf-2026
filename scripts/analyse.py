#!/usr/bin/env python3
"""Tables, figure data and inline numbers for the bucket-separable search paper."""

import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ttstats as t  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--out", default=None,
                    help="where tables/ and figures/ are written; defaults to the article directory "
                         "above this one when it holds a main.tex, and otherwise to this directory, "
                         "so that the application stands alone when published on its own")
    args = ap.parse_args()
    if args.out is None:
        args.out = ".." if os.path.exists(os.path.join("..", "main.tex")) else "."
    R, TAB, FIG = args.results, os.path.join(args.out, "tables"), os.path.join(args.out, "figures")
    # The records published in results/ are complete by definition, and a fresh clone gives every
    # file the time of the clone, so the "written in the last two minutes" guard (ttstats.complete)
    # applies only to other directories -- a run of one's own, still being written.
    published = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
    if os.path.realpath(R) == os.path.realpath(published):
        os.environ.setdefault("TT_MIN_AGE", "0")
    os.makedirs(TAB, exist_ok=True)
    os.makedirs(FIG, exist_ok=True)
    # Everything in those two directories is generated here.  Clearing them first means a table or a
    # figure whose results file has been withdrawn -- because the experiment that produced it turned
    # out to measure the wrong thing and is being re-run -- cannot survive on disk and go on
    # appearing in the paper, sourced from data that no longer exists.
    t.reset_outputs(TAB, FIG)
    facts = {}

    # --- Experiment 1: the cost of one candidate --------------------------------------------------
    path = os.path.join(R, "cost.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[int(r["n"])].append(r)
        crows, dat = [], []
        for n in sorted(by):
            v = by[n]
            ns = t.median([t.num(r, "ns_per_candidate") for r in v])
            bu = t.median([t.num(r, "buckets_per_candidate") for r in v])
            mb = t.median([t.num(r, "mean_bucket") for r in v])
            mx = max(t.num(r, "max_bucket") for r in v) if "max_bucket" in v[0] else None
            p99 = (max(t.num(r, "p99_bucket") for r in v)
                   if "p99_bucket" in v[0] else None)
            ent = t.median([t.num(r, "entities") for r in v])
            crows.append([n, int(ent), t.fmt(mb, 2),
                          "--" if p99 is None else t.fmt(p99, 0),
                          "--" if mx is None else t.fmt(mx, 0),
                          t.fmt(bu, 2), t.fmt(ns, 0), len(v)])
            dat.append([n, round(bu, 3), round(ns, 1), round(mb, 3)])
        t.write_table(
            os.path.join(TAB, "cost.tex"),
            "The cost of one probe (a relocation applied and undone) against instance size, "
            "medians over 10 instances per size",
            "tab:cost",
            ["$n$", "entities", "mean bucket", "p99 bucket", "max bucket", "buckets/probe",
             "ns/probe", "runs"],
            crows,
            align="rrrrrrrr",
            note="\\emph{mean}, \\emph{p99} and \\emph{max bucket}: sizes of the non-empty "
                 "(entity, day) buckets of the planted schedule before the probe stream (p99 and "
                 "max are the largest over the 10 instances). \\emph{buckets/probe}: buckets "
                 "recomputed per probe, forward move and rollback together. "
                 "\\emph{ns/probe}: wall-clock time per probe.",
        )
        t.write_dat(FIG + "/cost.dat", ["n", "buckets", "ns", "mean_bucket"], dat)
        if dat:
            b0, bN = dat[0][1], dat[-1][1]
            n0, nN = dat[0][2], dat[-1][2]
            facts["costNmin"], facts["costNmax"] = dat[0][0], dat[-1][0]
            facts["costRange"] = t.fmt(dat[-1][0] / dat[0][0], 0)
            facts["bucketsMin"], facts["bucketsMax"] = t.fmt(b0, 2), t.fmt(bN, 2)
            facts["bucketsGrowthPct"] = t.fmt(100 * (bN - b0) / b0, 1)
            facts["nsMin"], facts["nsMax"] = t.fmt(n0, 0), t.fmt(nN, 0)
            facts["nsGrowthPct"] = t.fmt(100 * (nN - n0) / n0, 1)
            facts["meanBucketMin"] = t.fmt(min(d[3] for d in dat), 2)
            facts["meanBucketMax"] = t.fmt(max(d[3] for d in dat), 2)
            if "max_bucket" in rows[0]:
                allmax = [t.num(r, "max_bucket") for r in rows]
                facts["maxBucketMin"] = t.fmt(min(allmax), 0)
                facts["maxBucketMax"] = t.fmt(max(allmax), 0)
            # The maximum is one bucket of one instance; the 99th percentile says whether the
            # growth the maximum shows is the tail moving or the whole upper end of the
            # distribution moving, which is the difference between a constant with a bad outlier
            # and a constant that is not one.
            if "p99_bucket" in rows[0]:
                allp99 = [t.num(r, "p99_bucket") for r in rows]
                facts["pNNBucketMin"] = t.fmt(min(allp99), 0)
                facts["pNNBucketMax"] = t.fmt(max(allp99), 0)

    # --- Experiment 2: three evaluators -----------------------------------------------------------
    path = os.path.join(R, "evaluators.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[(int(r["n"]), r["evaluator"])].append(t.num(r, "ns_per_candidate"))
        ns = sorted({k[0] for k in by})
        erows, dat = [], []
        for n in ns:
            b = t.median(by.get((n, "bucketed"), [float("nan")]))
            u = t.median(by.get((n, "unbucketed"), [float("nan")]))
            fl = t.median(by.get((n, "full"), [float("nan")]))
            erows.append(
                [n, t.fmt(b, 0), t.fmt(u, 0), t.fmt(fl, 0), t.fmt(u / b, 1), t.fmt(fl / b, 1)]
            )
            dat.append([n, round(b, 1), round(u, 1), round(fl, 1)])
        t.write_table(
            os.path.join(TAB, "evaluators.tex"),
            "Three evaluators of a relocated schedule, median nanoseconds per probe",
            "tab:evaluators",
            ["$n$", "bucketed", "weekly", "full", "$\\times$ weekly", "$\\times$ full"],
            erows,
            align="rrrrrr",
            note="\\emph{full}: the independent validator re-reads the whole schedule; "
                 "\\emph{weekly}: the counts are kept but every affected entity is recomputed "
                 "over the whole week; \\emph{bucketed}: the evaluator of this paper. The "
                 "three streams draw relocations from the same distribution but are not the same "
                 "sequence (see the text).",
        )
        t.write_dat(FIG + "/evaluators.dat", ["n", "bucketed", "unbucketed", "full"], dat)
        if dat:
            facts["speedupFullMax"] = t.fmt(max(d[3] / d[1] for d in dat if d[1] > 0), 0)
            facts["speedupUnbucketedMax"] = t.fmt(max(d[2] / d[1] for d in dat if d[1] > 0), 1)
            facts["speedupUnbucketedMin"] = t.fmt(min(d[2] / d[1] for d in dat if d[1] > 0), 1)
            # Heuristic share of a probe spent on bucket recomputation, (R-1)/(k-1), if the weekly
            # variant recomputes k = 3 to 6 times as many buckets (text of Experiment 2): the
            # smallest ratio against the largest factor, and the largest against the smallest.
            rs = [d[2] / d[1] for d in dat if d[1] > 0]
            facts["evalShareMin"] = t.fmt((min(rs) - 1) / (6 - 1), 2)
            facts["evalShareMax"] = t.fmt((max(rs) - 1) / (3 - 1), 2)
            # How flat is each evaluator in n?  The bucketed one is the claim of Theorem th:eval;
            # the weekly one drifts because its bucket array is larger and misses cache more often.
            bs = [d[1] for d in dat]
            us = [d[2] for d in dat]
            facts["evalBucketedMin"] = t.fmt(min(bs), 0)
            facts["evalBucketedMax"] = t.fmt(max(bs), 0)
            facts["evalUnbucketedMin"] = t.fmt(min(us), 0)
            facts["evalUnbucketedMax"] = t.fmt(max(us), 0)
            facts["evalBucketedDrift"] = t.fmt(100.0 * (max(bs) - min(bs)) / min(bs), 0)
            facts["evalUnbucketedDrift"] = t.fmt(100.0 * (max(us) - min(us)) / min(us), 0)

    # --- Experiment 3: scaling --------------------------------------------------------------------
    path = os.path.join(R, "scaling.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[int(r["n"])].append(r)
        srows, dat = [], []
        for n in sorted(by):
            v = by[n]
            soft = [t.num(r, "soft") for r in v]
            hard = [t.num(r, "hard") for r in v]
            ref = t.median([t.num(r, "planted_soft") for r in v])
            srows.append(
                [
                    n,
                    len(v),
                    t.fmt(t.median(hard), 0),
                    t.fmt(t.median(soft), 0),
                    t.fmt(t.iqr(soft), 1),
                    t.fmt(ref, 0),
                    t.fmt(ref / max(1.0, t.median(soft)), 1),
                    t.fmt(t.median([t.num(r, "elapsed_ms") for r in v]) / 1000.0, 1),
                ]
            )
            dat.append(
                [
                    n,
                    round(t.median(soft), 2),
                    round(t.quantile(soft, 0.25), 2),
                    round(t.quantile(soft, 0.75), 2),
                    round(ref, 1),
                ]
            )
        t.write_table(
            os.path.join(TAB, "scaling.tex"),
            "Quality after 300\\,000 applied candidates against instance size",
            "tab:scaling",
            ["$n$", "runs", "hard", "soft", "IQR", "planted", "$\\times$ planted", "s"],
            srows,
            align="rrrrrrrr",
            note="\\emph{hard}: median $H$; \\emph{soft}: median of "
                 "$\\rho_7(\\Pi_7)+\\rho_8(\\Pi_8)+\\rho_9(\\Pi_9)$, "
                 "the sum of the three rounded soft counts (not the weighted objective $f$); "
                 "\\emph{planted}: the same sum for the hidden schedule the instance was read "
                 "off, a feasibility witness rather than a good timetable; \\emph{s}: median "
                 "wall-clock seconds per run.",
        )
        t.write_dat(FIG + "/scaling.dat", ["n", "soft", "q25", "q75", "planted"], dat)
        feasible = sum(1 for r in rows if t.num(r, "hard") == 0)
        facts["scalingRuns"] = len(rows)
        facts["scalingNmax"] = str(max(int(float(r["n"])) for r in rows))
        facts["scalingFeasible"] = feasible
        facts["scalingFeasiblePct"] = t.fmt(100.0 * feasible / max(1, len(rows)), 1)
        facts["scalingClean"] = sum(1 for r in rows if r["clean"] == "1")
        if dat:
            facts["scalingGainMin"] = t.fmt(min(d[4] / max(1.0, d[1]) for d in dat), 1)
            facts["scalingGainMax"] = t.fmt(max(d[4] / max(1.0, d[1]) for d in dat), 1)
            facts["scalingGainArgmax"] = str(
                max(dat, key=lambda d: d[4] / max(1.0, d[1]))[0])

    # --- The budget ladder: reported inside Experiment 3, not as an experiment of its own ---------
    path = os.path.join(R, "anytime.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(lambda: collections.defaultdict(list))
        for r in rows:
            by[int(r["n"])][int(r["candidates"])].append(t.num(r, "objective"))
        for n in sorted(by):
            out = []
            for c in sorted(by[n]):
                v = by[n][c]
                out.append(
                    [c, round(t.median(v), 1), round(t.quantile(v, 0.25), 1), round(t.quantile(v, 0.75), 1)]
                )
            t.write_dat(FIG + f"/anytime-n{n}.dat", ["candidates", "median", "q25", "q75"], out)
            if out:
                # The sentence this feeds says "between the smallest and the largest budget", so
                # the first point must be the smallest budget, not the second.
                first, last = out[0], out[-1]
                tag = "EightHundred" if n == 800 else "ThirtyTwoHundred"
                facts["anytimeGain" + tag] = t.fmt(first[1] / max(1.0, last[1]), 1)
                # The trajectory is sampled, not a ladder of separate runs, so the sentence that
                # quotes the ratio also says between which two samples it is taken, and how wide the
                # band across seeds is at the last one.
                facts["anytimeFirst" + tag] = f"{int(first[0]):,}".replace(",", "\\,")
                facts["anytimeLast" + tag] = f"{int(last[0]):,}".replace(",", "\\,")
                facts["anytimeRelIQRLast" + tag] = t.fmt(
                    100.0 * (last[3] - last[2]) / max(1.0, last[1]), 0)
                facts["anytimeSeeds"] = str(len(by[n][int(last[0])]))
                # What the last doubling of the budget bought: the sample nearest half the last.
                half = min(out, key=lambda o: abs(o[0] - last[0] / 2))
                facts["anytimeLastDoublingPct" + tag] = t.fmt(
                    100.0 * (1.0 - last[1] / max(1.0, half[1])), 0)

    # --- Experiment 4: the permutation operator ---------------------------------------------------
    path = os.path.join(R, "permute.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[(int(r["n"]), r["arm"])].append(r)
        prows = []
        order = ["off", "occupied", "widened", "no-identity-check"]
        for n in sorted({k[0] for k in by}):
            for arm in order:
                v = by.get((n, arm))
                if not v:
                    continue
                # Pooled over the cell's runs, not a median of per-run rates: a rate whose
                # denominator varies by an order of magnitude between arms has to be weighted by
                # that denominator, and this is also the statistic the text quotes.
                applied = sum(t.num(r, "kopt_applied") for r in v) / len(v)
                acc = sum(t.num(r, "kopt_accepted") for r in v) / len(v)
                imp = sum(t.num(r, "kopt_improved") for r in v) / len(v)
                prows.append(
                    [
                        n,
                        arm,
                        t.fmt(t.median([t.num(r, "soft") for r in v]), 0),
                        t.fmt(t.median([t.num(r, "objective") for r in v]), 0),
                        t.fmt(applied, 0),
                        t.fmt(100 * acc / applied, 1) if applied else "--",
                        t.fmt(100 * imp / applied, 2) if applied else "--",
                        t.fmt(t.median([t.num(r, "kopt_weight") for r in v]), 2),
                        len(v),
                    ]
                )
        # The cost the bandit divides by: work per application of the permutation operator against
        # work per application of a single relocation, on the arm where both are active.
        # The mechanical facts the paper quotes: how much the identity check removes, and what the
        # bandit ends up thinking of the operator with and without it.
        for arm, tag in (("occupied", "Occupied"), ("no-identity-check", "Unchecked")):
            v = [r for r in rows if r["arm"] == arm and int(r["n"]) == 800]
            if v:
                ap = sum(t.num(r, "kopt_applied") for r in v)
                im = sum(t.num(r, "kopt_improved") for r in v)
                facts["permuteImproved" + tag] = t.fmt(100 * im / max(1.0, ap), 2)
                facts["permuteApplied" + tag] = t.fmt(t.median(
                    [t.num(r, "kopt_applied") for r in v]), 0)
                facts["permuteWeight" + tag] = t.fmt(t.median(
                    [t.num(r, "kopt_weight") for r in v]), 2)
        # Per size.  Pooling the two cells hides a factor of three: the operator's matrix is
        # k-by-k over whatever the victim selector returned, and that is larger at the larger size.
        for n in sorted({k[0] for k in by}):
            v = by.get((n, "occupied"), [])
            if not v or "move_applied" not in v[0]:
                continue
            ka = sum(t.num(r, "kopt_applied") for r in v)
            kw = sum(t.num(r, "kopt_work") for r in v)
            ma = sum(t.num(r, "move_applied") for r in v)
            mw = sum(t.num(r, "move_work") for r in v)
            if ka and ma and mw:
                tag = "Small" if n == min(k[0] for k in by) else "Large"
                facts["permuteCostRatio" + tag] = t.fmt((kw / ka) / (mw / ma), 0)
                facts["permuteWorkPerApply" + tag] = t.fmt(kw / ka, 0)
                facts["permuteMoveWork" + tag] = t.fmt(mw / ma, 1)
        occ = [r for r in rows if r["arm"] == "occupied"]
        if occ:
            ka = sum(t.num(r, "kopt_applied") for r in occ)
            kw = sum(t.num(r, "kopt_work") for r in occ)
            ma = sum(t.num(r, "move_applied") for r in occ) if "move_applied" in occ[0] else 0
            mw = sum(t.num(r, "move_work") for r in occ) if "move_work" in occ[0] else 0
            if ka and ma and mw:
                facts["permuteCostRatio"] = t.fmt((kw / ka) / (mw / ma), 0)
                facts["permuteWorkPerApply"] = t.fmt(kw / ka, 1)
                facts["moveWorkPerApply"] = t.fmt(mw / ma, 2)
        t.write_table(
            os.path.join(TAB, "permute.tex"),
            "Arms of the permutation operator after 200\\,000 applied candidates",
            "tab:permute",
            ["$n$", "arm", "soft", "$f$", "applied", "accepted \\%", "improved \\%",
             "weight", "runs"],
            prows,
            align="rlrrrrrrr",
            small=True,
            note="\\emph{occupied}: the pool is the placements the members hold (reference "
                 "configuration); \\emph{widened}: free placements added; "
                 "\\emph{no-identity-check}: proposals that return every member to its own "
                 "placement are kept; \\emph{off}: operator disabled. \\emph{soft}, $f$ and "
                 "\\emph{weight} (final selection weight): medians; \\emph{applied}: mean "
                 "applications per run; \\emph{accepted}, \\emph{improved}: rates pooled over "
                 "the runs of a cell.",
        )
        # Paired tests against the "occupied" arm, Holm-corrected WITHIN each instance size, which
        # is what the caption and the prose both promise.  Correcting across both sizes at once
        # inflates every p by the number of comparisons in the other size.
        for n in sorted({k[0] for k in by}):
            tests = []
            base = {r["seed"]: t.num(r, "objective") for r in by.get((n, "occupied"), [])}
            for arm in order:
                if arm == "occupied" or (n, arm) not in by:
                    continue
                other = {r["seed"]: t.num(r, "objective") for r in by[(n, arm)]}
                keys = sorted(set(base) & set(other))
                if len(keys) < 5:
                    continue
                a = [base[k] for k in keys]
                b = [other[k] for k in keys]
                _, p = t.wilcoxon_signed_rank(a, b)
                tests.append((f"{n}:{arm}", p))
                # The series convention: A12 is the probability that a run of the ARM is worse than a
                # run of the reference, so above one half means the arm is worse.
                facts[f"permA12{n}{arm.replace('-', '')}"] = t.fmt(t.vargha_delaney(b, a), 3)
            for label, p in t.holm(tests):
                facts["permP" + label.replace(":", "").replace("-", "")] = t.fmt_p(p)

    # --- Experiment 5: where to aim a large neighbourhood -----------------------------------------
    path = os.path.join(R, "aiming.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[(int(r["n"]), r["selector"])].append(r)
        order = ["uniform", "relatedness", "entity-day", "community", "worst-window"]
        arows = []
        for n in sorted({k[0] for k in by}):
            base = {r["seed"]: t.num(r, "objective") for r in by.get((n, "community"), [])}
            tests = []
            for sel in order:
                v = by.get((n, sel))
                if not v:
                    continue
                objs = [t.num(r, "objective") for r in v]
                other = {r["seed"]: t.num(r, "objective") for r in v}
                keys = sorted(set(base) & set(other))
                p = float("nan")
                a12 = float("nan")
                if sel != "community" and len(keys) >= 5:
                    _, p = t.wilcoxon_signed_rank([base[k] for k in keys], [other[k] for k in keys])
                    a12 = t.vargha_delaney([other[k] for k in keys], [base[k] for k in keys])
                    tests.append((sel, p))
                arows.append(
                    [
                        n,
                        sel,
                        len(v),
                        t.fmt(t.median([t.num(r, "soft") for r in v]), 0),
                        t.fmt(t.median(objs), 0),
                        t.fmt(t.iqr(objs), 0),
                        t.fmt_p(p),
                        t.fmt(a12, 3),
                    ]
                )
            tag = "EightHundred" if n == 800 else "ThirtyTwoHundred"
            # The table must show the corrected p, since that is what the caption promises and what
            # the prose quotes.
            adjusted = dict(t.holm(tests))
            for row in arows:
                if row[0] == n and row[1] in adjusted:
                    row[6] = t.fmt_p(adjusted[row[1]])
            for sel, pv in t.holm(tests):
                facts["aimP" + tag + sel.replace("-", "")] = t.fmt_p(pv)
            for row in arows:
                if row[0] == n and row[1] != "community":
                    facts["aimA" + tag + str(row[1]).replace("-", "")] = row[7]
            facts["modularityCommunity" + ("EightHundred" if n==800 else "ThirtyTwoHundred")] = t.fmt(
                t.median([t.num(r, "modularity_community") for r in by.get((n, "community"), [])]), 3
            )
            facts["modularityLecturer" + ("EightHundred" if n==800 else "ThirtyTwoHundred")] = t.fmt(
                t.median([t.num(r, "modularity_lecturer") for r in by.get((n, "community"), [])]), 3
            )
            facts["communities" + ("EightHundred" if n==800 else "ThirtyTwoHundred")] = t.fmt(
                t.median([t.num(r, "communities") for r in by.get((n, "community"), [])]), 0
            )
        t.write_table(
            os.path.join(TAB, "aiming.tex"),
            "Removal selectors of ruin-and-recreate after 200\\,000 applied candidates",
            "tab:aiming",
            ["$n$", "selector", "runs", "soft", "$f$", "IQR", "$p$", "$\\hat A_{12}$"],
            # The names the text uses; the records keep the names the program gave them.
            [[r[0], {"worst-window": "window", "entity-day": "bucket"}.get(r[1], r[1])] + r[2:]
             for r in arows],
            align="rlrrrrrr",
            small=True,
            note="Each selector is compared with the community selector on the same 10 "
                 "instances. $p$: two-sided Wilcoxon signed-rank test on $f$, Holm-corrected "
                 "within each size; $\\hat A_{12}$: probability that a run of the selector has a "
                 "larger $f$ than a run of the community selector (above one half means worse). "
                 "\\emph{soft}, $f$: medians; IQR of $f$.",
        )

    # --- Experiment 6: the standard benchmark -----------------------------------------------------
    best, optimal = {}, {}
    bkpath = os.path.join("data-ctt", "best-known.csv")
    if os.path.exists(bkpath):
        for line in open(bkpath):
            if line.startswith("#") or line.startswith("instance"):
                continue
            col = line.strip().split(",")
            if len(col) < 2:
                continue
            best[col[0]] = int(col[1])
            # Whether the collected bound is PROVED optimal, which is a different statement from
            # "nobody has done better" and changes what a gap against it means.
            optimal[col[0]] = len(col) >= 4 and col[3] == "1"

    path = os.path.join(R, "ctt.csv")
    arms = ["bucket-local", "separable-aware", "fully-aware"]
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[(r["file"], r["arm"])].append(r)
        files = sorted({k[0] for k in by})
        # A file that is merely quiet is not a finished experiment: a single instance of this study
        # can run for minutes, so the age heuristic alone would let a table of the first two
        # instances into the paper.  The design is a full crossing, so require it.
        seeds = max((len(v) for v in by.values()), default=0)
        full = all(len(by.get((fn, a), [])) == seeds for fn in files for a in arms)
        if not full:
            print(f"  skipping ctt.csv: {len(rows)} rows, design not yet complete")
            files = []
        brows = []
        gaps = collections.defaultdict(list)
        gapsOpt = collections.defaultdict(list)
        feasAll, runsAll = 0, 0
        for fn in files:
            row = [fn]
            ref = best.get(fn)
            base = by.get((fn, arms[0]), [])
            row.append(str(int(t.num(base[0], "lectures"))) if base else "--")
            # A dagger marks a value the collection reports as proved optimal, so that a reader can
            # tell a gap to an optimum from a gap to the best anyone has found.
            row.append(("--" if ref is None
                        else (f"{ref}$^\\dagger$" if optimal.get(fn) else str(ref))))
            for arm in arms:
                v = by.get((fn, arm), [])
                if not v:
                    row += ["--"]
                    continue
                costs = [t.num(r, "cost") for r in v]
                feas = sum(1 for r in v if t.num(r, "hard") == 0 and t.num(r, "unplaced") == 0)
                feasAll += feas
                runsAll += len(v)
                row.append(t.fmt(min(costs), 0))
                if ref is not None and ref > 0:
                    gaps[arm].append(min(costs) / ref)
                    if optimal.get(fn):
                        gapsOpt[arm].append(min(costs) / ref)
            brows.append(row)
        # Two blocks side by side: twenty-one instances in one column would be a table taller than
        # the discussion it supports, and the rows are independent of one another.
        half = (len(brows) + 1) // 2
        wide = []
        for i in range(half):
            left = brows[i]
            right = brows[i + half] if i + half < len(brows) else [""] * len(brows[0])
            wide.append(list(left) + [""] + list(right))
        t.write_table(
            os.path.join(TAB, "ctt.tex"),
            "Official cost on the curriculum-based instances of ITC-2007, best of three seeds",
            "tab:ctt",
            ["instance", "lect.", "ref.", "local", "sep.", "full", "",
             "instance", "lect.", "ref.", "local", "sep.", "full"],
            wide, align="lrrrrrclrrrrr", small=True,
            note="Scored by the independent validator. The arms differ only in what the candidate "
                 "scorer reads besides the buckets a candidate touches: \\emph{local}, no soft "
                 "criterion; \\emph{sep.}, the two bucket-separable criteria; \\emph{full}, all "
                 "four. \\emph{ref.}: the upper bound collected in~\\cite{ceschia2023}, not a "
                 "result of this work; a dagger marks the \\cttOptimalCount{} values reported "
                 "there as proved optimal.")
        if runsAll:
            facts["cttRuns"] = str(runsAll)
            facts["cttFeasible"] = str(feasAll)
            facts["cttInstances"] = str(len(files))
            facts["cttOptimalCount"] = str(sum(1 for fn in files if optimal.get(fn)))
            facts["cttOpenCount"] = str(sum(1 for fn in files if fn in best and not optimal.get(fn)))
            facts["cttOpenNames"] = ", ".join(
                "\\textsf{%s}" % fn for fn in files if fn in best and not optimal.get(fn))
        TAG = {arms[0]: "Local", arms[1]: "Sep", arms[2]: "Full"}
        # A ratio to a zero reference is undefined, so instances whose reference is zero are left
        # out of the ratios; the text reports them by absolute cost instead.
        facts["cttGapN"] = str(sum(1 for fn in files if best.get(fn) not in (None, 0)))
        facts["cttGapOptN"] = str(sum(1 for fn in files if optimal.get(fn) and best.get(fn)))
        zeroRef = [fn for fn in files if best.get(fn) == 0]
        if zeroRef:
            fn = zeroRef[0]
            facts["cttZeroRefName"] = "\\textsf{%s}" % fn
            for arm in arms:
                v = [t.num(r, "cost") for r in by.get((fn, arm), [])]
                if v:
                    facts["cttZeroRefBest" + TAG[arm]] = t.fmt(min(v), 0)
        for arm in arms:
            if gaps[arm]:
                facts["cttGapMedian" + TAG[arm]] = t.fmt(t.median(gaps[arm]), 1)
                facts["cttGapMax" + TAG[arm]] = t.fmt(max(gaps[arm]), 1)
            # The same gap over the instances whose bound is PROVED optimal.  On those the ratio is
            # a distance from the optimum; on the other four it is a distance from the best value
            # anyone has reported, which is a weaker thing and should not be pooled with it silently.
            if gapsOpt[arm]:
                facts["cttGapOptMedian" + TAG[arm]] = t.fmt(t.median(gapsOpt[arm]), 1)
                facts["cttGapOptMax" + TAG[arm]] = t.fmt(max(gapsOpt[arm]), 1)
        # The two steps, each paired by instance and seed.  local -> sep. is what better bookkeeping
        # inside the separable class buys; sep. -> full is what lies outside it, and is the number
        # this section exists for.
        # Inference is made over distinct problems, not over files: comp03 and comp15 are the same
        # problem under renamed identifiers, and their nine records agree exactly, so counting both
        # would count one observation twice.  The classes come from data-ctt/equivalence.csv, which
        # ships with the results, so the analysis does not depend on the third-party .ctt files;
        # when the files are present the manifest is checked against them.  Every file in ctt.csv
        # must have exactly one entry, and every class must have the full design, or the analysis
        # stops rather than writing numbers from a partial or wrong grouping.
        import ctt_equivalence
        try:
            problem_of = ctt_equivalence.read_manifest("data-ctt")
        except (OSError, ValueError) as err:
            sys.exit(f"analyse.py: cannot read data-ctt/equivalence.csv ({err}); it is required for "
                     "the instance-level analysis of ctt.csv")
        missing_entry = [fn for fn in files if fn not in problem_of]
        if missing_entry:
            sys.exit("analyse.py: no equivalence.csv entry for " + ", ".join(missing_entry))
        present = ctt_equivalence.classes("data-ctt") if os.path.isdir("data-ctt") else []
        if present:
            if sum(len(c) for c in present) != len(problem_of):
                sys.exit("analyse.py: data-ctt holds a different set of .ctt files from equivalence.csv; "
                         "run scripts/ctt_equivalence.py data-ctt --check")
            for c in present:
                if any(problem_of.get(f) != c[0] for f in c):
                    sys.exit("analyse.py: equivalence.csv disagrees with the .ctt files on " + "/".join(c))
        keep = sorted({problem_of[fn] for fn in files})
        if any(k not in files for k in keep):
            sys.exit("analyse.py: a representative in equivalence.csv has no records in ctt.csv")
        dup = sorted({tuple(sorted(f for f in files if problem_of[f] == k)) for k in keep}, key=str)
        dup = [c for c in dup if len(c) > 1]
        facts["cttDistinct"] = str(len(keep))
        facts["cttEquivNames"] = "; ".join(" and ".join("\\textsf{%s}" % f for f in c) for c in dup) or "none"
        instTests = []

        def step(lo, hi, tag):
            # The 63 run pairs: descriptive counts only, since runs on one instance are not
            # independent observations about the benchmark.
            pairs = []
            for fn in files:
                a = {r["seed"]: t.num(r, "cost") for r in by.get((fn, lo), [])}
                b = {r["seed"]: t.num(r, "cost") for r in by.get((fn, hi), [])}
                for k in sorted(set(a) & set(b)):
                    pairs.append((a[k], b[k]))
            if len(pairs) < 5:
                return
            x = [p[0] for p in pairs]
            y = [p[1] for p in pairs]
            facts["cttBetter" + tag] = str(sum(1 for u, w in pairs if w < u))
            facts["cttWorse" + tag] = str(sum(1 for u, w in pairs if w > u))
            facts["cttPairs"] = str(len(pairs))
            # A ratio of the two pooled medians, over runs on different instances: descriptive only,
            # and the text says so.
            facts["cttGain" + tag] = t.fmt(
                100.0 * (1.0 - t.median(y) / max(1e-9, t.median(x))), 0)
            # Per distinct problem: the median over seeds of each arm.  This is the inference.
            lo_m, hi_m, rel = [], [], []
            for fn in keep:
                a = [t.num(r, "cost") for r in by.get((fn, lo), [])]
                b = [t.num(r, "cost") for r in by.get((fn, hi), [])]
                if not a or not b:
                    continue
                ma, mb = t.median(a), t.median(b)
                lo_m.append(ma)
                hi_m.append(mb)
                if ma > 0:
                    rel.append(100.0 * (1.0 - mb / ma))
            facts["cttInstBetter" + tag] = str(sum(1 for u, w in zip(lo_m, hi_m) if w < u))
            facts["cttInstWorse" + tag] = str(sum(1 for u, w in zip(lo_m, hi_m) if w > u))
            facts["cttInstTie" + tag] = str(sum(1 for u, w in zip(lo_m, hi_m) if w == u))
            _, pi = t.wilcoxon_signed_rank(lo_m, hi_m)
            instTests.append((tag, pi))
            facts["cttInstPRaw" + tag] = t.fmt_p(pi)
            if rel:
                facts["cttInstGain" + tag] = t.fmt(t.median(rel), 0)
            # The same summary over all 21 files, for the sensitivity sentence.
            rel21 = []
            for fn in files:
                a = [t.num(r, "cost") for r in by.get((fn, lo), [])]
                b = [t.num(r, "cost") for r in by.get((fn, hi), [])]
                if a and b and t.median(a) > 0:
                    rel21.append(100.0 * (1.0 - t.median(b) / t.median(a)))
            if rel21:
                facts["cttFileGain" + tag] = t.fmt(t.median(rel21), 0)
        step(arms[0], arms[1], "SepStep")
        step(arms[1], arms[2], "FullStep")
        step(arms[0], arms[2], "BothSteps")
        # The three steps are one family: Holm across the instance-level tests.
        for tag, pv in t.holm(instTests):
            facts["cttInstP" + tag] = t.fmt_p(pv)
        # Where the residual cost sits.  The claim in the text is that the two criteria outside
        # Definition 1 carry most of it, so the split has to be a measured number rather than an
        # impression: S1 and S3 are the bucket-separable pair, S2 and S4 the aggregate pair, and the
        # official weights are 1, 5, 2 and 1.
        for arm in arms:
            v = [r for k, rs in by.items() if k[1] == arm for r in rs]
            if not v:
                continue
            sep = sum(t.num(r, "s1") + 2 * t.num(r, "s3") for r in v)
            agg = sum(5 * t.num(r, "s2") + t.num(r, "s4") for r in v)
            if sep + agg > 0:
                facts["cttAggShare" + TAG[arm]] = t.fmt(100.0 * agg / (sep + agg), 0)
            # The four criteria separately, at their official weights.  The second step turns out
            # to be a trade rather than an improvement -- it lowers the two criteria it newly sees
            # and raises one of the two it already saw -- and that cannot be read off a single
            # aggregate share.
            for tag, w, col in (("SOne", 1, "s1"), ("STwo", 5, "s2"),
                                ("SThree", 2, "s3"), ("SFour", 1, "s4")):
                facts["ctt" + tag + TAG[arm]] = t.fmt(
                    sum(w * t.num(r, col) for r in v), 0)
        # What the second arm costs in time, per instance rather than pooled: the probe it performs
        # at proposal time is O(curricula of the course), so the overhead is not a constant across a
        # family whose curricula-per-course ratio varies by an order of magnitude.
        # A run stops early when it reaches a perfect schedule, so its time is not the time of a
        # full budget; an instance with such a run is left out of the time ratio and named instead.
        ratios, early = [], []
        for fn in files:
            runs = [r for a in arms for r in by.get((fn, a), [])]
            if any(t.num(r, "candidates") < t.num(r, "budget") for r in runs):
                early.append(fn)
                continue
            a = [t.num(r, "elapsed_ms") for r in by.get((fn, arms[0]), [])]
            b = [t.num(r, "elapsed_ms") for r in by.get((fn, arms[2]), [])]
            if a and b and t.median(a) > 0:
                ratios.append(t.median(b) / t.median(a))
        facts["cttEarlyStopNames"] = ", ".join("\\textsf{%s}" % fn for fn in early) or "none"
        facts["cttEarlyStopCount"] = str(len(early))
        stopped = [t.num(r, "candidates") for fn in early for a in arms for r in by.get((fn, a), [])
                   if t.num(r, "candidates") < t.num(r, "budget")]
        if stopped:
            facts["cttEarlyStopMaxCand"] = f"{int(max(stopped)):,}".replace(",", "\\,")
        if ratios:
            facts["cttTimeRatioMin"] = t.fmt(min(ratios), 1)
            facts["cttTimeRatioMax"] = t.fmt(max(ratios), 1)
            facts["cttTimeRatioMedian"] = t.fmt(t.median(ratios), 1)
        for arm in arms:
            v = [r for k, rs in by.items() if k[1] == arm for r in rs]
            if v:
                tag = TAG[arm]
                facts["cttMs" + tag] = t.fmt(t.median([t.num(r, "elapsed_ms") for r in v]) / 1000, 1)
                facts["cttWork" + tag] = t.fmt(
                    t.median([t.num(r, "work") for r in v]) / 1e6, 0)

    # The scan-defect diagnostic (built with -DTT_SCAN_DIAG=ON): per run, the scans made and the
    # scans on which the pruning rule used before the correction would have returned a worse
    # placement.  Pooled per arm.
    path = os.path.join(R, "diagnostics", "scan-defect.csv")
    if os.path.exists(path):
        rows = t.read(path)
        tag = {"bucket-local": "Local", "separable-aware": "Sep", "fully-aware": "Full"}
        for arm, tg in tag.items():
            v = [r for r in rows if r["arm"] == arm]
            scans = sum(t.num(r, "scans") for r in v)
            if scans:
                facts["cttDiagPct" + tg] = t.fmt(100.0 * sum(t.num(r, "worse") for r in v) / scans, 1)
        if rows:
            facts["cttDiagBudget"] = f"{int(t.num(rows[0], 'budget')):,}".replace(",", "\\,")

    path = os.path.join(R, "ctt-cost.csv")
    if t.complete(path):
        rows = t.read(path)
        by = collections.defaultdict(list)
        for r in rows:
            by[r["arm"]].append(r)
        crows = []
        for arm in ("buckets", "buckets+aggregates"):
            v = sorted(by.get(arm, []), key=lambda r: t.num(r, "lectures"))
            if not v:
                continue
            ns = [t.num(r, "ns_per_candidate") for r in v]
            bk = [t.num(r, "buckets_per_candidate") for r in v]
            crows.append([arm, len(v), t.fmt(min(bk), 2), t.fmt(max(bk), 2), t.fmt(min(ns), 0),
                          t.fmt(max(ns), 0)])
            tag = "Buckets" if arm == "buckets" else "Both"
            facts["cttCostBucketMin" + tag] = t.fmt(min(bk), 2)
            facts["cttCostBucketMax" + tag] = t.fmt(max(bk), 2)
            facts["cttCostNsMin" + tag] = t.fmt(min(ns), 0)
            facts["cttCostNsMax" + tag] = t.fmt(max(ns), 0)
            # The theorem says the count does not grow with the instance, not that it is the same
            # number on every instance: its constant is set by how many entities a class touches,
            # which on this family is how many curricula a course belongs to.  So the endpoints of
            # the SIZE ordering are what tests the theorem, and the spread between the extreme
            # instances is a fact about the family rather than about n.  v is already sorted by
            # lectures.
            facts["cttCostBucketAtSmallest" + tag] = t.fmt(bk[0], 2)
            facts["cttCostBucketAtLargest" + tag] = t.fmt(bk[-1], 2)
            facts["cttCostLectMin" + tag] = t.fmt(t.num(v[0], "lectures"), 0)
            facts["cttCostLectMax" + tag] = t.fmt(t.num(v[-1], "lectures"), 0)
            hi = max(range(len(bk)), key=lambda i: bk[i])
            facts["cttCostBucketArgmaxLect" + tag] = t.fmt(t.num(v[hi], "lectures"), 0)
        if crows:
            # No table: the two rows are quoted in the prose, which is all they support.
            # How much the non-separable half adds per candidate.
            if "cttCostNsMaxBoth" in facts and "cttCostNsMaxBuckets" in facts:
                allb = [t.num(r, "ns_per_candidate") for r in by.get("buckets", [])]
                allt = [t.num(r, "ns_per_candidate") for r in by.get("buckets+aggregates", [])]
                if allb and allt:
                    facts["cttAggregateOverhead"] = t.fmt(
                        100.0 * (t.median(allt) / t.median(allb) - 1.0), 0)

    # A p-value quoted in the prose as "p = ..." would print "p = <0.001" for the smallest ones, so
    # every p macro also has a relation form, "= 0.012" or "< 0.001", for use as $p\fooRel$.
    for k in [k for k in facts if re.match(r"^(cttInstP|permP|aimP)", k)]:
        v = str(facts[k])
        facts[k + "Rel"] = ("<" + v[3:]) if v.startswith("$<$") else ("=" + v)
    t.write_macros(os.path.join(TAB, "facts.tex"), facts)
    print(f"wrote {len(facts)} macros")


if __name__ == "__main__":
    main()
