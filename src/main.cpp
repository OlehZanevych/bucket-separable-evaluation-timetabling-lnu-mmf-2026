// Experiments for "Bucket-separable evaluation and a large-neighbourhood search for large-scale
// university course timetabling" (Visnyk of the Lviv University. Series Mechanics and Mathematics,
// 2026).  The numbers are the article's experiment numbers.
//
//   cost        Exp. 1  the cost of one candidate, in buckets and in nanoseconds, against n
//   evaluators  Exp. 2  three evaluators of a relocated schedule, on streams from one distribution
//   scaling     Exp. 3  quality at a fixed candidate budget, against n
//   anytime     Exp. 3  quality against budget, with dispersion (Fig. 1)
//   permute     Exp. 4  what the exact assignment sub-problem buys
//   aiming      Exp. 5  where a large neighbourhood should be aimed
//   ctt         Exp. 6  the ITC-2007 curriculum-based instances (src/ctt.cpp)
//   ctt-cost    Exp. 6  the cost of one relocation on those instances
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "core/generate.hpp"
#include "core/io.hpp"
#include "core/rng.hpp"
#include "core/search.hpp"
#include "core/selftest.hpp"
#include "core/state.hpp"
#include "ctt.hpp"
#include "core/validate.hpp"

using namespace tt;
using Clock = std::chrono::steady_clock;

namespace {

std::string gResults = "results";

int gSeedFrom = 1;   ///< --seedfrom S: run seeds S..N only, so that one design can be split over processes

std::ofstream openCsv(const std::string& name, const std::string& header) {
  // A results directory that does not exist used to lose every record without a word; create it,
  // and stop if the file still cannot be opened.
  std::filesystem::create_directories(gResults);
  const std::string path = gResults + "/" + name;
  std::ofstream f(path);
  if (!f) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    std::exit(1);
  }
  // Unbuffered: one run is one line and runs are seconds apart, so flushing costs nothing and a
  // long experiment can be watched, or salvaged, while it is still running.
  f << std::unitbuf;
  f << header << "\n";
  return f;
}

GeneratedInstance makeInstance(int n, uint64_t seed, double roomSlack = 1.15) {
  GenOptions go;
  go.nClasses = n;
  go.seed = seed;
  go.roomSlack = roomSlack;
  return generateInstance(go);
}

/// A stream of single-class relocations applied to a state, timed.  This is the measurement the
/// central claim rests on: the cost of one candidate must not grow with the number of classes.
struct ProbeResult {
  double nsPerCandidate = 0;
  double bucketsPerCandidate = 0;
  int64_t candidates = 0;
};

ProbeResult probeCandidateCost(const Instance& in, const std::vector<Placement>& start,
                               uint64_t seed, int64_t candidates) {
  State st(in);
  st.setAll(start);
  Rng rng(seed);
  const std::vector<int32_t>& movable = in.movable;
  const int64_t work0 = st.work();
  // Warm the cache with a short unmeasured burst.
  for (int64_t i = 0; i < 20000; ++i) {
    const int c = movable[rng.below(static_cast<uint32_t>(movable.size()))];
    const ClassSpec& cs = in.classes[c];
    if (cs.slots.empty()) continue;
    const Slot& s = cs.slots[rng.below(static_cast<uint32_t>(cs.slots.size()))];
    const int r = cs.rooms.empty() ? -1 : cs.rooms[rng.below(static_cast<uint32_t>(cs.rooms.size()))];
    const Placement old = st.placementOf(c);
    st.place(c, Placement{static_cast<int8_t>(s.day), static_cast<int16_t>(s.time), s.parity, r});
    st.flush();
    st.place(c, old);
    st.flush();
  }
  const int64_t workStart = st.work();
  const auto t0 = Clock::now();
  int64_t done = 0;
  for (int64_t i = 0; i < candidates; ++i) {
    const int c = movable[rng.below(static_cast<uint32_t>(movable.size()))];
    const ClassSpec& cs = in.classes[c];
    if (cs.slots.empty()) continue;
    const Slot& s = cs.slots[rng.below(static_cast<uint32_t>(cs.slots.size()))];
    const int r = cs.rooms.empty() ? -1 : cs.rooms[rng.below(static_cast<uint32_t>(cs.rooms.size()))];
    const Placement old = st.placementOf(c);
    st.place(c, Placement{static_cast<int8_t>(s.day), static_cast<int16_t>(s.time), s.parity, r});
    st.flush();
    volatile double sink = st.surrogate();
    (void)sink;
    st.place(c, old);   // reject: exactly what the search does when the candidate is refused
    st.flush();
    ++done;
  }
  const auto t1 = Clock::now();
  (void)work0;
  ProbeResult pr;
  pr.candidates = done;
  pr.nsPerCandidate = std::chrono::duration<double, std::nano>(t1 - t0).count() / std::max<int64_t>(1, done);
  pr.bucketsPerCandidate = static_cast<double>(st.work() - workStart) / std::max<int64_t>(1, done);
  return pr;
}

/// Weights for the benchmark study.  Every one of the series' own nine terms is switched off, so
/// the objective the search descends is exactly lambda * H plus the official UD2 cost carried by
/// the ExtraTerms; feasibility is still lexicographic, because hard() sums the counts rather than
/// the weighted counts.
constexpr double kCttBeta[kNumTerms] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

// =================================================================================================
// The standard benchmark: curriculum-based course timetabling, ITC-2007 track 3, formulation UD2.
//
// The rest of this paper studies an objective that is separable over (entity, day) buckets.  The
// formulation the field benchmarks on is separable in two of its four soft constraints and not in
// the other two, so running on it is not a formality but a test of how far the decomposition
// reaches.  What is reported is the official UD2 cost, computed by a validator that reads the .ctt
// file and shares no code with the incremental evaluator.

/// Check the incremental soft terms against the independent validator along a random walk, on every
/// instance given.  A disagreement here is either a fault in the incremental maintenance or a fault
/// in the mapping, and the two are told apart by which of the eight counts diverges.
int cttSelfTest(const std::vector<std::string>& paths) {
  int bad = 0;
  for (const std::string& path : paths) {
    tt::ctt::Problem prob = tt::ctt::readProblem(path);
    tt::ctt::Mapped mapped = tt::ctt::mapToInstance(prob);
    if (static_cast<int>(mapped.instance.nClasses()) != prob.totalLectures()) {
      std::printf("  %s: mapped %d classes for %d lectures\n", path.c_str(),
                  mapped.instance.nClasses(), prob.totalLectures());
      ++bad;
      continue;
    }
    tt::ctt::SoftTerms soft(prob, mapped);
    State st(mapped.instance, kCttBeta);
    st.setExtraTerms(&soft);

    // Seeded from the instance's name, not from std::hash of its path: std::hash differs between
    // standard libraries and the path between machines, and the walk should be the same everywhere.
    uint64_t nameHash = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char ch : prob.name) nameHash = (nameHash ^ ch) * 1099511628211ULL;
    Rng rng(0xC77 + nameHash);
    const int n = mapped.instance.nClasses();
    for (int step = 0; step < 400; ++step) {
      const int c = static_cast<int>(rng.below(n));
      const auto& slots = mapped.instance.classes[c].slots;
      Placement p;
      if (!slots.empty() && rng.below(10) != 0) {
        const Slot& sl = slots[rng.below(slots.size())];
        p.day = static_cast<int8_t>(sl.day);
        p.time = static_cast<int16_t>(sl.time);
        p.parity = sl.parity;
        p.room = static_cast<int32_t>(rng.below(std::max(1, mapped.instance.nRooms)));
      }
      // Exact invertibility, which is what the candidate probe relies on: apply the move, then
      // its inverse, and require every term to return to the value it held.
      const Placement previous = st.placementOf(c);
      const double before = soft.total();
      st.place(c, p);
      st.flush();
      st.place(c, previous);
      st.flush();
      if (soft.total() != before) {
        std::printf("  %s step %d: soft terms not invertible (%g -> %g)\n", path.c_str(), step,
                    before, soft.total());
        ++bad;
        break;
      }
      // The O(1) delta must equal what actually happens: compute it, apply the move, and compare
      // against the change in the aggregate part.  A hint that disagrees with the maintenance would
      // steer the proposal machinery by a number the objective does not honour.
      soft.setSight(tt::ctt::SoftTerms::Sight::All);
      const double predictedAll = soft.delta(c, st.placementOf(c), p);
      soft.setSight(tt::ctt::SoftTerms::Sight::Separable);
      const double predictedSep = soft.delta(c, st.placementOf(c), p);
      soft.setSight(tt::ctt::SoftTerms::Sight::None);
      const double allBefore = soft.total();
      const double sepBefore = soft.roomCapacity() + 2.0 * soft.compactness();
      st.place(c, p);
      st.flush();
      const double actualAll = soft.total() - allBefore;
      const double actualSep = soft.roomCapacity() + 2.0 * soft.compactness() - sepBefore;
      if (std::abs(actualAll - predictedAll) > 1e-9 || std::abs(actualSep - predictedSep) > 1e-9) {
        std::printf("  %s step %d: delta all %g vs %g, separable %g vs %g\n", path.c_str(), step,
                    predictedAll, actualAll, predictedSep, actualSep);
        ++bad;
        break;
      }
      if (step % 37 != 0) continue;

      const tt::ctt::Score ref = tt::ctt::validate(prob, mapped, st.placements());
      const bool agree = ref.s1 == soft.roomCapacity() && ref.s2 == soft.minWorkingDays() &&
                         ref.s3 == soft.compactness() && ref.s4 == soft.roomStability();
      // The hard counts are not compared value for value, and should not be.  The validator counts
      // each violated constraint once; the bucket evaluator counts clash *incidences*, and one
      // pair of lectures of the same course collides on the course entity, on the teacher's, and
      // on every curriculum the course belongs to.  What must hold -- and what feasibility means
      // here -- is that the two vanish together.  H4 cannot occur at all, because unavailability
      // is folded into the time domains and no operator can leave them.
      const bool hardAgree = (ref.h1 + ref.h2 + ref.h3 == 0) == (std::llround(st.hard()) == 0) &&
                             ref.h4 == 0;
      if (!agree || !hardAgree) {
        std::printf("  %s step %d: validator (%d,%d,%d,%d h=%d) evaluator (%d,%d,%d,%d h=%d)\n",
                    path.c_str(), step, ref.s1, ref.s2, ref.s3, ref.s4, ref.hard(),
                    soft.roomCapacity(), soft.minWorkingDays(), soft.compactness(),
                    soft.roomStability(), static_cast<int>(std::llround(st.hard())));
        ++bad;
        break;
      }
    }
  }
  return bad;
}

/// The evaluation-cost claim, measured on the standard instances rather than on generated ones.
///
/// Theorem "eval" says the exact cost of scoring one relocation is bounded independently of the
/// number of classes.  Every number supporting it so far comes from a generated family; these are
/// real faculties, with the entity structure a real faculty has.  The same probe is run twice per
/// instance: once over the bucket terms alone, and once with the non-separable soft terms attached,
/// so that what the aggregate maintenance adds per candidate is measured rather than argued.
void expCttCost(long long candidates, const std::vector<std::string>& paths) {
  auto f = openCsv("ctt-cost.csv",
                   "instance,file,arm,courses,lectures,entities,ns_per_candidate,"
                   "buckets_per_candidate");
  for (const std::string& path : paths) {
    tt::ctt::Problem prob = tt::ctt::readProblem(path);
    tt::ctt::Mapped mapped = tt::ctt::mapToInstance(prob);
    std::string stem = path.substr(path.find_last_of('/') + 1);
    stem = stem.substr(0, stem.find_last_of('.'));

    // A feasible starting schedule, so that the probe measures the cost of scoring candidates
    // around a schedule of the kind a search actually holds.
    SearchOptions so;
    so.timeLimitMs = 600000;
    so.candidateLimit = 200000;
    so.threads = 1;
    so.seed = 5150;
    so.extraFactory = [&prob, &mapped]() {
      return std::make_unique<tt::ctt::SoftTerms>(prob, mapped);
    };
    SearchResult seedRun = solve(mapped.instance, so, kCttBeta);

    for (const char* arm : {"buckets", "buckets+aggregates"}) {
      const bool withExtra = std::string(arm) == "buckets+aggregates";
      tt::ctt::SoftTerms soft(prob, mapped);
      State st(mapped.instance, kCttBeta);
      if (withExtra) st.setExtraTerms(&soft);
      st.setAll(seedRun.best);

      Rng rng(7788);
      const std::vector<int32_t>& movable = mapped.instance.movable;
      for (int64_t i = 0; i < 20000; ++i) {   // warm the caches
        const int c = movable[rng.below(static_cast<uint32_t>(movable.size()))];
        const ClassSpec& cs = mapped.instance.classes[c];
        if (cs.slots.empty()) continue;
        const Slot& sl = cs.slots[rng.below(static_cast<uint32_t>(cs.slots.size()))];
        const int r = cs.rooms.empty()
                          ? -1
                          : cs.rooms[rng.below(static_cast<uint32_t>(cs.rooms.size()))];
        const Placement old = st.placementOf(c);
        st.place(c, Placement{static_cast<int8_t>(sl.day), static_cast<int16_t>(sl.time),
                              sl.parity, r});
        st.flush();
        st.place(c, old);
        st.flush();
      }
      const int64_t workStart = st.work();
      const auto t0 = Clock::now();
      int64_t done = 0;
      for (int64_t i = 0; i < candidates; ++i) {
        const int c = movable[rng.below(static_cast<uint32_t>(movable.size()))];
        const ClassSpec& cs = mapped.instance.classes[c];
        if (cs.slots.empty()) continue;
        const Slot& sl = cs.slots[rng.below(static_cast<uint32_t>(cs.slots.size()))];
        const int r = cs.rooms.empty()
                          ? -1
                          : cs.rooms[rng.below(static_cast<uint32_t>(cs.rooms.size()))];
        const Placement old = st.placementOf(c);
        st.place(c, Placement{static_cast<int8_t>(sl.day), static_cast<int16_t>(sl.time),
                              sl.parity, r});
        st.flush();
        volatile double sink = st.surrogate();
        (void)sink;
        st.place(c, old);
        st.flush();
        ++done;
      }
      const auto t1 = Clock::now();
      const double ns =
          std::chrono::duration<double, std::nano>(t1 - t0).count() / std::max<int64_t>(1, done);
      const double buckets =
          static_cast<double>(st.work() - workStart) / std::max<int64_t>(1, done);
      f << prob.name << "," << stem << "," << arm << "," << prob.nCourses() << ","
        << prob.totalLectures() << "," << mapped.instance.nEntities() << "," << ns << ","
        << buckets << "\n";
    }
    std::printf("  %-8s cost probe done\n", stem.c_str());
    std::fflush(stdout);
  }
  std::printf("ctt-cost done\n");
}

/// Solve every instance given, at a budget counted in candidate evaluations, and report the UD2
/// cost the independent validator assigns to what came back.
void expCtt(int seeds, long long budget, const std::vector<std::string>& paths,
            const std::string& onlyArm) {
  auto f = openCsv("ctt.csv",
                   "instance,file,arm,seed,courses,lectures,rooms,periods,curricula,budget,hard,"
                   "unplaced,cost,s1,s2,s3,s4,candidates,work,elapsed_ms,sep_updates,agg_updates");
  for (const std::string& path : paths) {
    tt::ctt::Problem prob = tt::ctt::readProblem(path);
    tt::ctt::Mapped mapped = tt::ctt::mapToInstance(prob);
    std::string stem = path.substr(path.find_last_of('/') + 1);
    stem = stem.substr(0, stem.find_last_of('.'));
    // Three arms.  "bucket-local" is the search as the rest of the paper describes it: a candidate
    // is scored from the buckets it touches, so the official soft terms are invisible until the
    // acceptance test.  "separable-aware" lets the re-insertion scorer read the two bucket-separable
    // soft terms, and "fully-aware" all four, the other two through their per-course aggregates.
    for (const char* arm : {"bucket-local", "separable-aware", "fully-aware"}) {
      const std::string a = arm;
      if (!onlyArm.empty() && a != onlyArm) continue;
      const auto sight = a == "bucket-local"     ? tt::ctt::SoftTerms::Sight::None
                         : a == "separable-aware" ? tt::ctt::SoftTerms::Sight::Separable
                                                  : tt::ctt::SoftTerms::Sight::All;
      for (int seed = gSeedFrom; seed <= seeds; ++seed) {
        SearchOptions so;
        so.timeLimitMs = 3600000;
        so.candidateLimit = budget;
        so.threads = 1;
        so.seed = 91000 + seed;
        // The benchmark objective is the official one and nothing else: every weight of the
        // series' own nine terms is zero, so what the search descends is lambda * H + UD2.
        so.extraFactory = [&prob, &mapped, sight]() {
          auto p = std::make_unique<tt::ctt::SoftTerms>(prob, mapped);
          p->setSight(sight);
          return p;
        };
        SearchResult r = solve(mapped.instance, so, kCttBeta);
        const tt::ctt::Score sc = tt::ctt::validate(prob, mapped, r.best);
        f << prob.name << "," << stem << "," << arm << "," << seed << "," << prob.nCourses() << ","
          << prob.totalLectures() << "," << prob.nRooms() << "," << prob.periods() << ","
          << prob.curricula.size() << "," << budget << "," << sc.hard() << "," << sc.unplaced
          << "," << sc.cost() << "," << sc.s1 << "," << sc.s2 << "," << sc.s3 << "," << sc.s4
          << "," << r.candidates << "," << r.workUnits << "," << r.elapsedMs << ","
          << (r.extraCounters.size() > 0 ? r.extraCounters[0] : 0) << ","
          << (r.extraCounters.size() > 1 ? r.extraCounters[1] : 0) << "\n";
        std::printf("  %-8s %-16s seed %2d  H=%d  UD2=%d\n", stem.c_str(), arm, seed, sc.hard(),
                    sc.cost());
#ifdef TT_SCAN_DIAG
        {
          long long calls = 0, worse = 0;
          scanDiag(calls, worse, true);
          std::printf("SCANDIAG,%s,%s,%d,%lld,%lld\n", stem.c_str(), arm, seed, calls, worse);
        }
#endif
        std::fflush(stdout);
      }
    }
  }
  std::printf("ctt done\n");
}

// =================================================================================================

void expCost(int seeds) {
  auto f = openCsv("cost.csv",
                   "n,seed,classes,entities,mean_bucket,max_bucket,p99_bucket,"
                   "ns_per_candidate,buckets_per_candidate");
  for (int n : {200, 400, 800, 1600, 3200, 6400, 12800, 25600}) {
    for (int seed = gSeedFrom; seed <= seeds; ++seed) {
      GeneratedInstance gi = makeInstance(n, seed);
      const Instance& in = gi.instance;
      // Bucket sizes over the non-empty buckets.  Theorem 3 squares the *largest* affected bucket,
      // so a mean is not enough to say the bound is not vacuous: the max and the 99th percentile
      // are recorded beside it.
      double occupied = 0, members = 0;
      int maxBucket = 0;
      std::vector<int> sizes;
      {
        State st(in);
        st.setAll(gi.planted);
        for (int e = 0; e < in.nEntities(); ++e)
          for (int d = 1; d <= in.nDays; ++d) {
            const int sz = st.stat(e, d).size;
            if (sz > 0) {
              occupied += 1;
              members += sz;
              maxBucket = std::max(maxBucket, sz);
              sizes.push_back(sz);
            }
          }
      }
      std::sort(sizes.begin(), sizes.end());
      const int p99 = sizes.empty()
                          ? 0
                          : sizes[std::min(sizes.size() - 1,
                                           static_cast<size_t>(0.99 * sizes.size()))];
      ProbeResult pr = probeCandidateCost(in, gi.planted, 555 + seed, 300000);
      f << n << "," << seed << "," << in.nClasses() << "," << in.nEntities() << ","
        << (occupied > 0 ? members / occupied : 0) << "," << maxBucket << "," << p99 << ","
        << pr.nsPerCandidate << ","
        << pr.bucketsPerCandidate << "\n";
    }
  }
  std::printf("cost done\n");
}

void expEvaluators(int seeds) {
  // Three ways to learn the objective of a neighbouring schedule:
  //   full        -- re-read the whole schedule, the way the independent validator does
  //   unbucketed  -- keep the counters but recompute every term of every affected entity over the
  //                  whole week rather than over the affected day
  //   bucketed    -- the method of this paper
  auto f = openCsv("evaluators.csv", "n,seed,evaluator,ns_per_candidate,candidates");
  for (int n : {200, 400, 800, 1600, 3200, 6400}) {
    for (int seed = gSeedFrom; seed <= seeds; ++seed) {
      GeneratedInstance gi = makeInstance(n, seed);
      const Instance& in = gi.instance;
      const int64_t budget = n <= 1600 ? 20000 : 6000;

      // bucketed
      {
        ProbeResult pr = probeCandidateCost(in, gi.planted, 17 + seed, budget * 10);
        f << n << "," << seed << ",bucketed," << pr.nsPerCandidate << "," << pr.candidates << "\n";
      }
      // full re-evaluation
      {
        Rng rng(17 + seed);
        std::vector<Placement> sigma = gi.planted;
        const auto t0 = Clock::now();
        for (int64_t i = 0; i < budget; ++i) {
          const int c = in.movable[rng.below(static_cast<uint32_t>(in.movable.size()))];
          const ClassSpec& cs = in.classes[c];
          if (cs.slots.empty()) continue;
          const Slot& s = cs.slots[rng.below(static_cast<uint32_t>(cs.slots.size()))];
          const int r = cs.rooms.empty() ? -1
                                         : cs.rooms[rng.below(static_cast<uint32_t>(cs.rooms.size()))];
          const Placement old = sigma[c];
          sigma[c] = Placement{static_cast<int8_t>(s.day), static_cast<int16_t>(s.time), s.parity, r};
          Score sc = validate(in, sigma);
          volatile double sink = sc.objective(kDefaultBeta);
          (void)sink;
          sigma[c] = old;
        }
        const auto t1 = Clock::now();
        f << n << "," << seed << ",full,"
          << std::chrono::duration<double, std::nano>(t1 - t0).count() / budget << "," << budget
          << "\n";
      }
      // unbucketed: the counters are kept, but every affected entity is recomputed over all days
      {
        State st(in);
        st.setAll(gi.planted);
        Rng rng(17 + seed);
        std::vector<int32_t> ents;
        const auto t0 = Clock::now();
        for (int64_t i = 0; i < budget; ++i) {
          const int c = in.movable[rng.below(static_cast<uint32_t>(in.movable.size()))];
          const ClassSpec& cs = in.classes[c];
          if (cs.slots.empty()) continue;
          const Slot& s = cs.slots[rng.below(static_cast<uint32_t>(cs.slots.size()))];
          const int r = cs.rooms.empty() ? -1
                                         : cs.rooms[rng.below(static_cast<uint32_t>(cs.rooms.size()))];
          const Placement old = st.placementOf(c);
          const Placement neu{static_cast<int8_t>(s.day), static_cast<int16_t>(s.time), s.parity, r};
          st.place(c, neu);
          // Touch every day of every affected entity, which is what an implementation that knows
          // the terms are separable by entity but not by day has to do.
          st.entitiesOf(c, neu, ents);
          std::vector<int32_t> more;
          st.entitiesOf(c, old, more);
          ents.insert(ents.end(), more.begin(), more.end());
          // Force a real recomputation of every bucket of every affected entity, which is what an
          // implementation that knows the terms are separable by entity but not by day must do.
          for (int32_t e : ents) st.markEntityWeekDirty(e);
          st.flush();
          volatile double sink = st.surrogate();
          (void)sink;
          st.place(c, old);
          st.flush();
        }
        const auto t1 = Clock::now();
        f << n << "," << seed << ",unbucketed,"
          << std::chrono::duration<double, std::nano>(t1 - t0).count() / budget << "," << budget
          << "\n";
      }
    }
  }
  std::printf("evaluators done\n");
}

void expScaling(int seeds, long long budget) {
  auto f = openCsv("scaling.csv",
                   "n,seed,budget,classes,planted_soft,planted_objective,hard,soft,objective,"
                   "unplaced,candidates,work,elapsed_ms,clean");
  for (int n : {200, 400, 800, 1600, 3200, 6400, 12800}) {
    for (int seed = gSeedFrom; seed <= seeds; ++seed) {
      GeneratedInstance gi = makeInstance(n, seed);
      Score ref = validate(gi.instance, gi.planted);
      SearchOptions so;
      so.timeLimitMs = 3600000;
      so.candidateLimit = budget;
      so.threads = 1;
      so.seed = 4242 + seed;
      SearchResult r = solve(gi.instance, so);
      Score sc = validate(gi.instance, r.best);
      f << n << "," << seed << "," << budget << "," << gi.instance.nClasses() << "," << ref.soft()
        << "," << ref.objective(kDefaultBeta) << "," << sc.hard() << "," << sc.soft() << ","
        << sc.objective(kDefaultBeta) << "," << sc.unplaced << "," << r.candidates << ","
        << r.workUnits << "," << r.elapsedMs << "," << (sc.clean() ? 1 : 0) << "\n";
    }
  }
  std::printf("scaling done\n");
}

void expAnytime(int seeds) {
  auto f = openCsv("anytime.csv", "n,seed,candidates,hard,soft,objective");
  for (int n : {800, 3200}) {
    for (int seed = gSeedFrom; seed <= seeds; ++seed) {
      GeneratedInstance gi = makeInstance(n, seed);
      SearchOptions so;
      so.timeLimitMs = 3600000;
      so.candidateLimit = 1000000;
      so.threads = 1;
      so.seed = 8080 + seed;
      so.sampleEveryCandidates = 20000;
      SearchResult r = solve(gi.instance, so);
      for (const TrajectoryPoint& t : r.trajectory)
        f << n << "," << seed << "," << t.candidates << "," << t.hard << "," << t.soft << ","
          << t.objective << "\n";
    }
  }
  std::printf("anytime done\n");
}

void expPermute(int seeds, long long budget) {
  auto f = openCsv("permute.csv",
                   "n,seed,arm,hard,soft,objective,candidates,kopt_applied,kopt_accepted,"
                   "kopt_improved,kopt_work,kopt_weight,move_applied,move_work");
  struct Arm { const char* name; bool useKopt; int extra; bool identity; };
  const Arm arms[] = {{"off", false, 0, true},
                      {"occupied", true, 0, true},
                      {"widened", true, 8, true},
                      {"no-identity-check", true, 0, false}};
  for (int n : {800, 3200}) {
    for (const Arm& a : arms) {
      for (int seed = gSeedFrom; seed <= seeds; ++seed) {
        GeneratedInstance gi = makeInstance(n, seed);
        SearchOptions so;
        so.timeLimitMs = 3600000;
        so.candidateLimit = budget;
        so.threads = 1;
        so.seed = 31337 + seed;
        so.useKopt = a.useKopt;
        so.koptExtra = a.extra;
        so.koptIdentityCheck = a.identity;
        SearchResult r = solve(gi.instance, so);
        Score sc = validate(gi.instance, r.best);
        const OperatorStat& k = r.ops[kKopt];
        const OperatorStat& mv = r.ops[kMove];
        f << n << "," << seed << "," << a.name << "," << sc.hard() << "," << sc.soft() << ","
          << sc.objective(kDefaultBeta) << "," << r.candidates << "," << k.applied << ","
          << k.accepted << "," << k.improved << "," << k.work << "," << k.finalWeight << ","
          << mv.applied << "," << mv.work << "\n";
      }
    }
  }
  std::printf("permute done\n");
}

void expAiming(int seeds, long long budget) {
  auto f = openCsv("aiming.csv",
                   "n,seed,selector,hard,soft,objective,candidates,modularity_community,"
                   "modularity_lecturer,communities");
  const char* names[] = {"uniform", "relatedness", "entity-day", "community", "worst-window"};
  for (int n : {800, 3200}) {
    for (int seed = gSeedFrom; seed <= seeds; ++seed) {
      GeneratedInstance gi = makeInstance(n, seed);
      const Instance& in = gi.instance;
      std::vector<int32_t> labels = buildClusters(in, 12345 + seed);
      int communities = 0;
      for (int32_t l : labels) communities = std::max(communities, l + 1);
      const double modCommunity = partitionModularity(in, labels);
      // The obvious declared partition to beat: group every class by its lecturer.
      std::vector<int32_t> byLecturer(in.nClasses(), 0);
      for (int c = 0; c < in.nClasses(); ++c)
        byLecturer[c] = in.classes[c].lecturers.empty() ? 0 : in.classes[c].lecturers[0];
      const double modLecturer = partitionModularity(in, byLecturer);

      for (int sel = 0; sel < 5; ++sel) {
        SearchOptions so;
        so.timeLimitMs = 3600000;
        so.candidateLimit = budget;
        so.threads = 1;
        so.seed = 606 + seed;
        so.ruinSelector = sel;
        SearchResult r = solve(in, so);
        Score sc = validate(in, r.best);
        f << n << "," << seed << "," << names[sel] << "," << sc.hard() << "," << sc.soft() << ","
          << sc.objective(kDefaultBeta) << "," << r.candidates << "," << modCommunity << ","
          << modLecturer << "," << communities << "\n";
      }
    }
  }
  std::printf("aiming done\n");
}

/// Re-run one (n, seed) of the scaling experiment and print the validator's full verdict.
///
/// The scaling table reports how many runs produced a schedule the independent validator accepted
/// as respecting every declared domain, availability window and load cap.  When that count is not
/// the number of runs, this is how one finds out which rule was broken and where, without
/// instrumenting the search.
void replay(int n, uint64_t seed, long long budget) {
  GenOptions go;
  go.nClasses = n;
  go.seed = seed;
  GeneratedInstance gi = generateInstance(go);
  SearchOptions so;
  so.timeLimitMs = 3600000;
  so.candidateLimit = budget;
  so.threads = 1;
  so.seed = 4242 + seed;
  SearchResult r = solve(gi.instance, so);
  const Score sc = validate(gi.instance, r.best);
  std::printf("n=%d seed=%llu  hard=%.0f soft=%.0f unplaced=%d\n", n, (unsigned long long)seed,
              sc.hard(), sc.soft(), sc.unplaced);
  std::printf("  domain breaches       %d\n", sc.domainBreaches);
  std::printf("  availability breaches %d\n", sc.availabilityBreaches);
  std::printf("  per-day cap breaches  %d\n", sc.capBreaches);
  std::printf("  immovable classes moved %d\n", sc.immovableMoved);
  for (int i = 0; i < 9; ++i) std::printf("  Pi_%d = %.3f\n", i + 1, sc.pi[i]);
}

void writeInstances(const std::string& dir) {
  // Every generated instance an experiment uses, with the schedule it was read off: sizes 200 to
  // 25 600 with seeds 1-10 (Experiments 1-5 use subsets of these) and seeds 11-15 at n = 800 and
  // 3200 (Experiment 4).  The experiments generate them in memory, so the files are not read by any
  // experiment; they let another solver work on exactly the same instances, and data/SHA256SUMS
  // lets anyone check that the generator still produces them (scripts/check_data.sh).
  std::filesystem::create_directories(dir);
  int checked = 0;
  auto write = [&](int n, int seed) {
    GeneratedInstance gi = makeInstance(n, seed);
    // The planted schedule must be a feasibility witness: no hard violation, nothing unplaced, and
    // no domain, availability or load-cap breach under the independent validator.
    const Score sc = validate(gi.instance, gi.planted);
    if (sc.hard() != 0 || sc.unplaced != 0 || !sc.clean()) {
      std::fprintf(stderr, "planted schedule of n=%d seed=%d is not feasible (H=%.0f, unplaced=%d)\n",
                   n, seed, sc.hard(), sc.unplaced);
      std::exit(1);
    }
    ++checked;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/n%05d-s%d.txt", dir.c_str(), n, seed);
    bool ok = writeInstance(path, gi.instance);
    std::snprintf(path, sizeof(path), "%s/n%05d-s%d.planted.txt", dir.c_str(), n, seed);
    ok = writeSchedule(path, gi.planted) && ok;
    if (!ok) {
      std::fprintf(stderr, "cannot write %s\n", path);
      std::exit(1);
    }
  };
  for (int n : {200, 400, 800, 1600, 3200, 6400, 12800, 25600})
    for (int seed = 1; seed <= 10; ++seed) write(n, seed);
  for (int n : {800, 3200})
    for (int seed = 11; seed <= 15; ++seed) write(n, seed);
  std::printf("%d instances written to %s; every planted schedule is feasible under the validator\n",
              checked, dir.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf(
        "usage: experiments <cost|evaluators|scaling|anytime|permute|aiming|ctt|ctt-cost|ctt-selftest|"
        "instances|replay|selftest> "
        "[--seeds N] [--seedfrom S] [--budget K] [--results DIR] [--data DIR] [--arm NAME]\n");
    return 2;
  }
  const std::string cmd = argv[1];
  int seeds = 20;
  long long budget = 300000;
  std::string data = "data";
  int replayN = 400;
  uint64_t replaySeed = 1;
  std::string onlyArm;   // ctt only: run one arm instead of all three
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--budget") && i + 1 < argc) budget = std::atoll(argv[++i]);
    else if (!std::strcmp(argv[i], "--maxbudget") && i + 1 < argc)
      tt::setBudgetCap(std::atoll(argv[++i]));
    else if (!std::strcmp(argv[i], "--results") && i + 1 < argc) gResults = argv[++i];
    else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
    else if (!std::strcmp(argv[i], "--arm") && i + 1 < argc) onlyArm = argv[++i];
    else if (!std::strcmp(argv[i], "--seedfrom") && i + 1 < argc) gSeedFrom = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--n") && i + 1 < argc) replayN = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) replaySeed = std::strtoull(argv[++i], nullptr, 10);
  }
  if (cmd == "ctt" || cmd == "ctt-selftest" || cmd == "ctt-cost") {
    std::vector<std::string> paths;
    for (int i = 1; i <= 21; ++i) {
      char buf[512];
      std::snprintf(buf, sizeof(buf), "%s/comp%02d.ctt", data.c_str(), i);
      std::ifstream probe(buf);
      if (probe) paths.push_back(buf);
    }
    if (paths.empty()) {
      std::printf("no comp*.ctt under %s; see README for where to put them\n", data.c_str());
      return 2;
    }
    if (cmd == "ctt-selftest") {
      const int bad = cttSelfTest(paths);
      std::printf(bad ? "ctt self-test FAILED on %d instance(s)\n" : "ctt self-test passed (%d)\n",
                  bad ? bad : static_cast<int>(paths.size()));
      return bad ? 1 : 0;
    }
    if (cmd == "ctt-cost") {
      expCttCost(budget, paths);
      return 0;
    }
    expCtt(seeds, budget, paths, onlyArm);
    return 0;
  }
  if (cmd == "cost") expCost(seeds);
  else if (cmd == "evaluators") expEvaluators(seeds);
  else if (cmd == "scaling") expScaling(seeds, budget);
  else if (cmd == "anytime") expAnytime(seeds);
  else if (cmd == "permute") expPermute(seeds, budget);
  else if (cmd == "aiming") expAiming(seeds, budget);
  else if (cmd == "instances") writeInstances(data);
  else if (cmd == "replay") replay(replayN, replaySeed, budget);
  else if (cmd == "selftest") {
    std::filesystem::create_directories(gResults);   // the round-trip check writes its files there
    return runSelfTest(gResults);
  }
  else {
    std::printf("unknown subcommand %s\n", cmd.c_str());
    return 2;
  }
  return 0;
}
