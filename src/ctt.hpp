// The standard curriculum-based course timetabling formulation (ITC-2007 track 3, UD2).
//
// This file exists to answer one question about the rest of the series: the objective studied
// throughout is bucket-separable, and the reader is entitled to ask what happens on the formulation
// the field actually benchmarks on.  Two of the four official soft constraints are bucket-separable
// and two are not, so the answer is not "it applies" or "it does not" but a measurable split, and
// this is the machinery that measures it.
//
//   S1 RoomCapacity          each student above the room's capacity                  1 point
//   S2 MinimumWorkingDays    each working day below the course's minimum             5 points
//   S3 CurriculumCompactness each lecture of a curriculum with no adjacent lecture   2 points
//   S4 RoomStability         each distinct room a course uses beyond the first       1 point
//
// S1 is a function of one (room, day) bucket and S3 of one (curriculum, day) bucket; both are
// therefore bucket-separable in the sense of the model, and an incremental evaluator over them
// costs what the separability lemma says.  S2 and S4 are functions of the set of days, and of the
// set of rooms, that a *course* occupies across the whole week: no function of a single
// (entity, day) bucket can express either, because both need to know what the course does on the
// other days.  They are carried here by a per-course aggregate maintained alongside the buckets.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/model.hpp"
#include "core/state.hpp"

namespace tt {
namespace ctt {

/// One course as the .ctt file declares it.
struct Course {
  std::string name;
  std::string teacher;
  int lectures = 0;
  int minWorkingDays = 0;
  int students = 0;
};

/// A curriculum is a named set of courses.
struct Curriculum {
  std::string name;
  std::vector<int> courses;   ///< indices into Problem::courses
};

/// The instance exactly as the file gives it, before any mapping onto the series' model.  The
/// validator below reads *this* and nothing else, so a fault in the mapping shows up as a
/// disagreement rather than as two copies of the same mistake.
struct Problem {
  std::string name;
  int days = 0, periodsPerDay = 0;
  std::vector<Course> courses;
  std::vector<Curriculum> curricula;
  std::vector<std::string> roomName;
  std::vector<int> roomCapacity;
  /// unavailable[course] holds forbidden period indices, period = day * periodsPerDay + slot.
  std::vector<std::vector<int>> unavailable;

  int periods() const { return days * periodsPerDay; }
  int nCourses() const { return static_cast<int>(courses.size()); }
  int nRooms() const { return static_cast<int>(roomCapacity.size()); }
  int totalLectures() const {
    int t = 0;
    for (const Course& c : courses) t += c.lectures;
    return t;
  }
};

/// Read a .ctt file in the ITC-2007 format.  Throws std::runtime_error on a malformed file, and
/// checks the declared counts against the sections actually present, because a silently truncated
/// instance would produce a plausible and wrong result.
Problem readProblem(const std::string& path);

/// The mapping onto the series' model.
///
/// One lecture is one class.  Teachers become lecturer entities; curricula become group entities;
/// and each course becomes a group entity of its own, which is how "no two lectures of one course
/// share a period" is expressed as an ordinary clash rather than as a special rule.  Rooms are
/// rooms, with every room admissible for every lecture, since the official formulation prices an
/// over-full room rather than forbidding it.  Unavailability is folded into the time domains and so
/// cannot be violated by any operator.
struct Mapped {
  Instance instance;
  std::vector<int> courseOfClass;   ///< which course each class is a lecture of
  int nCourses = 0;
};
Mapped mapToInstance(const Problem& p);

/// The four soft constraints, maintained incrementally against the same placements the buckets see.
///
/// S1 and S3 are bucket-separable and are recomputed per touched (entity, day) bucket; S2 and S4
/// are not, and are carried in the per-course aggregates `days_` and `rooms_`.  The split is the
/// point: `separableWork()` and `aggregateWork()` count the two kinds of update separately so the
/// study can report what the non-separable half costs.
class SoftTerms final : public ExtraTerms {
 public:
  SoftTerms(const Problem& p, const Mapped& m);

  void reset(const std::vector<Placement>& placements) override;
  void move(int c, const Placement& from, const Placement& to) override;
  double value() const override { return total(); }
  double soft() const override { return total(); }
  /// What the candidate scorer is allowed to see of the soft objective.
  ///
  /// The three settings are the experiment.  \c None reproduces the search the rest of the paper
  /// describes, whose scoring reads the buckets a move touches and no soft term at all.
  /// \c Separable adds the two criteria that \emph{are} functions of one bucket in this entity
  /// system --- room capacity and curriculum compactness.  \c All adds the two that are not, read
  /// from the per-course aggregates below.  The step from the first to the second is what reading
  /// the separable criteria buys; the step from the second to the third is what reading the other
  /// two, through bookkeeping of their own, buys on top.
  enum class Sight { None, Separable, All };
  void setSight(Sight s) { sight_ = s; }

  /// The change in whatever the scorer is allowed to see, if class `c` moved from `from` to `to`.
  double delta(int c, const Placement& from, const Placement& to) override {
    if (sight_ == Sight::None || c < 0 || c >= static_cast<int>(courseOf_.size())) return 0.0;
    const int course = courseOf_[c];
    double d = 0.0;

    // S1 room capacity and S3 curriculum compactness: both functions of one bucket, so both are
    // available to any scorer built on Definition 1.
    if (from.room >= 0)
      d -= std::max(0, p_.courses[course].students - p_.roomCapacity[from.room]);
    if (to.room >= 0)
      d += std::max(0, p_.courses[course].students - p_.roomCapacity[to.room]);
    d += 2.0 * (compactnessAfter(course, from, to) - s3_);

    if (sight_ == Sight::All) {
      // S2 minimum working days and S4 room stability: per-course aggregates, so a few lookups and
      // no dependence on how many curricula the course belongs to.
      const int need = p_.courses[course].minWorkingDays;
      int days = distinctDays_[course];
      if (from.placed() && dayCount_[static_cast<size_t>(course) * days_n_ + (from.day - 1)] == 1)
        --days;
      if (to.placed()) {
        const int at = dayCount_[static_cast<size_t>(course) * days_n_ + (to.day - 1)] -
                       ((from.placed() && from.day == to.day) ? 1 : 0);
        if (at == 0) ++days;
      }
      d += 5.0 * (std::max(0, need - days) - std::max(0, need - distinctDays_[course]));
      if (p_.nRooms() > 0) {
        int rooms = distinctRooms_[course];
        if (from.room >= 0 &&
            roomCount_[static_cast<size_t>(course) * p_.nRooms() + from.room] == 1)
          --rooms;
        if (to.room >= 0) {
          const int at = roomCount_[static_cast<size_t>(course) * p_.nRooms() + to.room] -
                         (from.room == to.room ? 1 : 0);
          if (at == 0) ++rooms;
        }
        d += std::max(0, rooms - 1) - std::max(0, distinctRooms_[course] - 1);
      }
    }
    return d;
  }

  /// A lower bound on delta() over the rooms of one slot: delta() with no destination room.  S2 and
  /// S3 do not depend on the room, so they are exact here; the room-capacity charge at the
  /// destination is never negative, and a destination room can only keep or raise the number of
  /// distinct rooms the course uses, so leaving it out can only lower S1 and S4.
  double deltaLowerBound(int c, const Placement& from, int day, int time, Parity parity) override {
    const Placement noRoom{static_cast<int8_t>(day), static_cast<int16_t>(time), parity, -1};
    return delta(c, from, noRoom);
  }

  /// The official UD2 cost, 1*S1 + 5*S2 + 2*S3 + 1*S4.
  double total() const { return s1_ + 5.0 * s2_ + 2.0 * s3_ + s4_; }
  int roomCapacity() const { return s1_; }
  int minWorkingDays() const { return s2_; }
  int compactness() const { return s3_; }
  int roomStability() const { return s4_; }

  /// Counters for the cost study: how many updates of each kind the maintenance has performed.
  long long separableUpdates() const { return sepUpdates_; }
  long long aggregateUpdates() const { return aggUpdates_; }
  /// {separable updates, aggregate updates}, for SearchResult::extraCounters.
  std::vector<int64_t> counters() const override { return {sepUpdates_, aggUpdates_}; }

 private:
  void applyOne(int c, const Placement& p, int sign);
  void recomputeCompactnessDay(int curriculum, int day);
  /// What s3_ would be after moving one lecture, computed by applying the occupancy change,
  /// rescoring the affected (curriculum, day) pairs and restoring both.
  int compactnessAfter(int course, const Placement& from, const Placement& to);

  const Problem& p_;
  int periodsPerDay_ = 0, days_n_ = 0;

  std::vector<int> courseOf_;                  ///< class -> course
  std::vector<std::vector<int>> curriculaOf_;  ///< course -> curricula containing it

  // --- the non-separable aggregates, per course --------------------------------------------
  std::vector<int> dayCount_;    ///< [course * days + day] lectures of the course on that day
  std::vector<int> distinctDays_;
  std::vector<int> roomCount_;   ///< [course * nRooms + room]
  std::vector<int> distinctRooms_;

  // --- the separable part ---------------------------------------------------------------------
  /// occupancy[(curriculum * days + day) * periodsPerDay + slot] lectures of that curriculum there
  std::vector<int> occ_;
  /// the compactness penalty currently attributed to one (curriculum, day) pair
  std::vector<int> compactOfDay_;

  Sight sight_ = Sight::None;
  int s1_ = 0, s2_ = 0, s3_ = 0, s4_ = 0;
  long long sepUpdates_ = 0, aggUpdates_ = 0;
};

/// Score a schedule from scratch against the *file's* data structures.
///
/// Shares no code with SoftTerms or with State: it re-reads the problem, walks the placements, and
/// counts each of the four soft terms and each of the four hard ones by the most direct route the
/// definitions allow.  This is the number reported, and the self-test requires it to agree with the
/// incremental evaluator term by term along a random walk.
struct Score {
  int h1 = 0;   ///< lectures of one course sharing a period, plus lectures left unplaced
  int h2 = 0;   ///< curriculum or teacher conflicts
  int h3 = 0;   ///< two lectures in one room in one period
  int h4 = 0;   ///< a lecture in a period the course declares unavailable
  int s1 = 0, s2 = 0, s3 = 0, s4 = 0;
  int unplaced = 0;

  int hard() const { return h1 + h2 + h3 + h4; }
  int cost() const { return s1 + 5 * s2 + 2 * s3 + s4; }
  bool feasible() const { return hard() == 0 && unplaced == 0; }
};
Score validate(const Problem& p, const Mapped& m, const std::vector<Placement>& placements);

}  // namespace ctt
}  // namespace tt
