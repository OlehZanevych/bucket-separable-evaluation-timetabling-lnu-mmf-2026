#include "ctt.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tt {
namespace ctt {
namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

}  // namespace

// =================================================================================================
// Reading

Problem readProblem(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);

  Problem p;
  int declCourses = -1, declRooms = -1, declCurricula = -1, declConstraints = -1;
  std::string line;
  std::string section;
  std::map<std::string, int> courseIndex, roomIndex;

  auto header = [&](const std::string& l, const char* key, int& out) {
    const std::string k = std::string(key) + ":";
    if (l.rfind(k, 0) != 0) return false;
    out = std::stoi(trim(l.substr(k.size())));
    return true;
  };

  while (std::getline(f, line)) {
    const std::string t = trim(line);
    if (t.empty()) continue;
    if (t == "END.") break;

    if (t.rfind("Name:", 0) == 0) { p.name = trim(t.substr(5)); continue; }
    if (header(t, "Courses", declCourses)) continue;
    if (header(t, "Rooms", declRooms)) continue;
    if (header(t, "Days", p.days)) continue;
    if (header(t, "Periods_per_day", p.periodsPerDay)) continue;
    if (header(t, "Curricula", declCurricula)) continue;
    if (header(t, "Constraints", declConstraints)) continue;

    if (t == "COURSES:" || t == "ROOMS:" || t == "CURRICULA:" ||
        t == "UNAVAILABILITY_CONSTRAINTS:") {
      section = t;
      continue;
    }

    std::istringstream in(t);
    if (section == "COURSES:") {
      Course c;
      if (!(in >> c.name >> c.teacher >> c.lectures >> c.minWorkingDays >> c.students))
        throw std::runtime_error("malformed COURSES line in " + path + ": " + t);
      courseIndex[c.name] = static_cast<int>(p.courses.size());
      p.courses.push_back(c);
    } else if (section == "ROOMS:") {
      std::string name;
      int cap = 0;
      if (!(in >> name >> cap))
        throw std::runtime_error("malformed ROOMS line in " + path + ": " + t);
      roomIndex[name] = static_cast<int>(p.roomName.size());
      p.roomName.push_back(name);
      p.roomCapacity.push_back(cap);
    } else if (section == "CURRICULA:") {
      Curriculum q;
      int n = 0;
      if (!(in >> q.name >> n))
        throw std::runtime_error("malformed CURRICULA line in " + path + ": " + t);
      for (int i = 0; i < n; ++i) {
        std::string cn;
        if (!(in >> cn)) throw std::runtime_error("short CURRICULA line in " + path + ": " + t);
        auto it = courseIndex.find(cn);
        if (it == courseIndex.end())
          throw std::runtime_error("curriculum " + q.name + " names unknown course " + cn);
        q.courses.push_back(it->second);
      }
      p.curricula.push_back(q);
    } else if (section == "UNAVAILABILITY_CONSTRAINTS:") {
      std::string cn;
      int day = 0, slot = 0;
      if (!(in >> cn >> day >> slot))
        throw std::runtime_error("malformed UNAVAILABILITY line in " + path + ": " + t);
      auto it = courseIndex.find(cn);
      if (it == courseIndex.end())
        throw std::runtime_error("unavailability names unknown course " + cn);
      if (p.unavailable.size() < p.courses.size()) p.unavailable.resize(p.courses.size());
      p.unavailable[it->second].push_back(day * p.periodsPerDay + slot);
    }
  }
  if (p.unavailable.size() < p.courses.size()) p.unavailable.resize(p.courses.size());

  // The declared counts are checked rather than trusted: a truncated download parses cleanly and
  // yields a plausible, wrong answer, which is the worst kind of failure for a benchmark run.
  int nUnav = 0;
  for (const auto& v : p.unavailable) nUnav += static_cast<int>(v.size());
  auto want = [&](const char* what, int got, int decl) {
    if (decl >= 0 && got != decl)
      throw std::runtime_error(path + ": declares " + std::to_string(decl) + " " + what +
                               " and holds " + std::to_string(got));
  };
  want("courses", static_cast<int>(p.courses.size()), declCourses);
  want("rooms", static_cast<int>(p.roomCapacity.size()), declRooms);
  want("curricula", static_cast<int>(p.curricula.size()), declCurricula);
  want("constraints", nUnav, declConstraints);
  if (p.days <= 0 || p.periodsPerDay <= 0) throw std::runtime_error(path + ": no period structure");
  return p;
}

// =================================================================================================
// Mapping onto the series' model

Mapped mapToInstance(const Problem& p) {
  Mapped out;
  Instance& in = out.instance;
  in.name = p.name;
  in.nDays = p.days;
  in.nBuildings = 1;
  in.travel.assign(1, 0);

  // Teachers, in order of first appearance, become lecturer entities.
  std::map<std::string, int> teacherIndex;
  for (const Course& c : p.courses)
    if (!teacherIndex.count(c.teacher)) {
      const int id = static_cast<int>(teacherIndex.size());
      teacherIndex[c.teacher] = id;
    }
  in.nLecturers = static_cast<int>(teacherIndex.size());

  // Group entities: one per curriculum, then one per course.  The course entity is what makes "two
  // lectures of the same course may not share a period" an ordinary clash.
  const int nCur = static_cast<int>(p.curricula.size());
  out.nCourses = p.nCourses();
  in.nGroups = nCur + out.nCourses;
  in.nRooms = p.nRooms();
  in.nAbstract = 0;

  // One bell grid, one period per time index, sixty minutes apart, so that "adjacent period" is
  // "adjacent time index" and the model's minute arithmetic agrees with the formulation's.
  for (int t = 0; t < p.periodsPerDay; ++t) in.times.push_back(TimeSpec{60 * t, 0});
  in.durations.assign(1, 60);

  in.roomBuilding.assign(in.nRooms, 0);
  in.roomCapacity = p.roomCapacity;
  in.maxPerDay.assign(in.nEntities(), 1 << 24);   // the formulation caps nothing per day

  std::vector<std::vector<int>> curriculaOf(out.nCourses);
  for (int q = 0; q < nCur; ++q)
    for (int c : p.curricula[q].courses) curriculaOf[c].push_back(q);

  for (int ci = 0; ci < out.nCourses; ++ci) {
    const Course& c = p.courses[ci];
    std::set<int> forbidden(p.unavailable[ci].begin(), p.unavailable[ci].end());
    for (int l = 0; l < c.lectures; ++l) {
      ClassSpec cs;
      cs.lecturers.push_back(teacherIndex.at(c.teacher));
      // Group-local ids for now; shifted into the shared entity index space below.
      for (int q : curriculaOf[ci]) cs.groups.push_back(q);
      cs.groups.push_back(nCur + ci);      // the course's own entity
      cs.kind = PlaceKind::Room;
      cs.duration = 60;
      cs.students = c.students;
      cs.movable = true;
      for (int d = 0; d < p.days; ++d)
        for (int t = 0; t < p.periodsPerDay; ++t) {
          if (forbidden.count(d * p.periodsPerDay + t)) continue;
          cs.slots.push_back(Slot{static_cast<uint8_t>(d + 1), static_cast<uint16_t>(t),
                                  Parity::Weekly});
        }
      for (int r = 0; r < in.nRooms; ++r) cs.rooms.push_back(r);   // capacity is priced, not forced
      out.courseOfClass.push_back(ci);
      in.classes.push_back(std::move(cs));
    }
  }
  // Group entity ids are offset by the number of lecturers in the shared index space.
  for (ClassSpec& cs : in.classes)
    for (int32_t& g : cs.groups) g = in.groupId(g);

  in.finalise();
  return out;
}

// =================================================================================================
// The four soft constraints, maintained incrementally

SoftTerms::SoftTerms(const Problem& p, const Mapped& m)
    : p_(p), periodsPerDay_(p.periodsPerDay), days_n_(p.days) {
  courseOf_ = m.courseOfClass;
  curriculaOf_.assign(p.nCourses(), {});
  for (size_t q = 0; q < p.curricula.size(); ++q)
    for (int c : p.curricula[q].courses) curriculaOf_[c].push_back(static_cast<int>(q));

  dayCount_.assign(static_cast<size_t>(p.nCourses()) * days_n_, 0);
  distinctDays_.assign(p.nCourses(), 0);
  roomCount_.assign(static_cast<size_t>(p.nCourses()) * std::max(1, p.nRooms()), 0);
  distinctRooms_.assign(p.nCourses(), 0);
  occ_.assign(static_cast<size_t>(p.curricula.size()) * days_n_ * periodsPerDay_, 0);
  compactOfDay_.assign(static_cast<size_t>(p.curricula.size()) * days_n_, 0);

  // Every course starts with no working days, so the whole minimum is unmet.
  s2_ = 0;
  for (const Course& c : p.courses) s2_ += c.minWorkingDays;
}

void SoftTerms::reset(const std::vector<Placement>& placements) {
  std::fill(dayCount_.begin(), dayCount_.end(), 0);
  std::fill(distinctDays_.begin(), distinctDays_.end(), 0);
  std::fill(roomCount_.begin(), roomCount_.end(), 0);
  std::fill(distinctRooms_.begin(), distinctRooms_.end(), 0);
  std::fill(occ_.begin(), occ_.end(), 0);
  std::fill(compactOfDay_.begin(), compactOfDay_.end(), 0);
  s1_ = s3_ = s4_ = 0;
  s2_ = 0;
  for (const Course& c : p_.courses) s2_ += c.minWorkingDays;
  for (size_t c = 0; c < placements.size() && c < courseOf_.size(); ++c)
    if (placements[c].placed()) applyOne(static_cast<int>(c), placements[c], +1);
}

void SoftTerms::move(int c, const Placement& from, const Placement& to) {
  if (c < 0 || c >= static_cast<int>(courseOf_.size())) return;
  if (from.placed()) applyOne(c, from, -1);
  if (to.placed()) applyOne(c, to, +1);
}

/// Add (sign = +1) or remove (sign = -1) one lecture at one placement.
void SoftTerms::applyOne(int c, const Placement& pl, int sign) {
  const int course = courseOf_[c];
  const int day = pl.day - 1;              // the model numbers days from one
  const int slot = pl.time;
  const int room = pl.room;

  // --- S1 RoomCapacity: a function of the (room, day) bucket, and of nothing else -------------
  if (room >= 0) {
    const int over = p_.courses[course].students - p_.roomCapacity[room];
    if (over > 0) s1_ += sign * over;
    ++sepUpdates_;
  }

  // --- S4 RoomStability: a function of the whole course, not of one bucket --------------------
  if (room >= 0) {
    int& n = roomCount_[static_cast<size_t>(course) * p_.nRooms() + room];
    const bool was = n > 0;
    n += sign;
    const bool now = n > 0;
    if (was != now) {
      const int before = std::max(0, distinctRooms_[course] - 1);
      distinctRooms_[course] += now ? 1 : -1;
      s4_ += std::max(0, distinctRooms_[course] - 1) - before;
    }
    ++aggUpdates_;
  }

  // --- S2 MinimumWorkingDays: also a function of the whole course -----------------------------
  {
    int& n = dayCount_[static_cast<size_t>(course) * days_n_ + day];
    const bool was = n > 0;
    n += sign;
    const bool now = n > 0;
    if (was != now) {
      const int before = std::max(0, p_.courses[course].minWorkingDays - distinctDays_[course]);
      distinctDays_[course] += now ? 1 : -1;
      const int after = std::max(0, p_.courses[course].minWorkingDays - distinctDays_[course]);
      s2_ += after - before;
    }
    ++aggUpdates_;
  }

  // --- S3 CurriculumCompactness: a function of the (curriculum, day) bucket --------------------
  for (int q : curriculaOf_[course]) {
    occ_[(static_cast<size_t>(q) * days_n_ + day) * periodsPerDay_ + slot] += sign;
    recomputeCompactnessDay(q, day);
    ++sepUpdates_;
  }
}

/// What s3_ would become if one lecture of `course` moved from `from` to `to`.
///
/// Exact, and restores every counter it touches.  Only the (curriculum, day) pairs the move touches
/// can change, so the cost is bounded by the curricula the course belongs to times the periods in a
/// day -- which is a bucket-local quantity, as it must be for a scorer built on Definition 1 to be
/// allowed to use it.
int SoftTerms::compactnessAfter(int course, const Placement& from, const Placement& to) {
  const std::vector<int>& qs = curriculaOf_[course];
  if (qs.empty()) return s3_;
  const int saved = s3_;
  std::vector<std::pair<size_t, int>> touched;   // (index into compactOfDay_, old value)
  auto bump = [&](const Placement& p, int sign) {
    if (!p.placed()) return;
    const int day = p.day - 1;
    for (int q : qs) {
      occ_[(static_cast<size_t>(q) * days_n_ + day) * periodsPerDay_ + p.time] += sign;
      const size_t key = static_cast<size_t>(q) * days_n_ + day;
      bool seen = false;
      for (const auto& kv : touched)
        if (kv.first == key) { seen = true; break; }
      if (!seen) touched.emplace_back(key, compactOfDay_[key]);
    }
  };
  bump(from, -1);
  bump(to, +1);
  for (const auto& kv : touched)
    recomputeCompactnessDay(static_cast<int>(kv.first / days_n_),
                            static_cast<int>(kv.first % days_n_));
  const int after = s3_;
  // Put everything back exactly as it was.
  bump(to, -1);
  bump(from, +1);
  for (const auto& kv : touched) {
    s3_ -= compactOfDay_[kv.first];
    compactOfDay_[kv.first] = kv.second;
    s3_ += kv.second;
  }
  s3_ = saved;
  return after;
}

/// The compactness penalty of one (curriculum, day) pair, recomputed from that pair's occupancy.
/// This is the bucket recomputation the separability lemma licenses: it reads one bucket's contents
/// and nothing else, and its cost is bounded by the number of periods in a day.
void SoftTerms::recomputeCompactnessDay(int q, int day) {
  const size_t base = (static_cast<size_t>(q) * days_n_ + day) * periodsPerDay_;
  int penalty = 0;
  for (int t = 0; t < periodsPerDay_; ++t) {
    const int here = occ_[base + t];
    if (here <= 0) continue;
    const bool left = t > 0 && occ_[base + t - 1] > 0;
    const bool right = t + 1 < periodsPerDay_ && occ_[base + t + 1] > 0;
    if (!left && !right) penalty += here;   // every lecture in an isolated period is penalised
  }
  int& stored = compactOfDay_[static_cast<size_t>(q) * days_n_ + day];
  s3_ += penalty - stored;
  stored = penalty;
}

// =================================================================================================
// The independent validator

Score validate(const Problem& p, const Mapped& m, const std::vector<Placement>& placements) {
  Score sc;
  const int P = p.periods();
  const int nC = p.nCourses();

  // Rebuild, from the placements alone, what each course does.
  std::vector<std::vector<int>> periodsOfCourse(nC);     // period index per placed lecture
  std::vector<std::vector<int>> roomsOfCourse(nC);
  std::vector<std::vector<int>> lecturesInPeriod(P);     // class indices
  for (size_t c = 0; c < placements.size() && c < m.courseOfClass.size(); ++c) {
    const Placement& pl = placements[c];
    const int course = m.courseOfClass[c];
    if (!pl.placed()) { ++sc.unplaced; continue; }
    const int day = pl.day - 1;
    const int period = day * p.periodsPerDay + pl.time;
    periodsOfCourse[course].push_back(period);
    roomsOfCourse[course].push_back(pl.room);
    lecturesInPeriod[period].push_back(static_cast<int>(c));
    // H4 Availability
    const auto& un = p.unavailable[course];
    if (std::find(un.begin(), un.end(), period) != un.end()) ++sc.h4;
  }

  // H1: two lectures of one course in one period.
  for (int c = 0; c < nC; ++c) {
    std::vector<int> ps = periodsOfCourse[c];
    std::sort(ps.begin(), ps.end());
    for (size_t i = 1; i < ps.size(); ++i)
      if (ps[i] == ps[i - 1]) ++sc.h1;
  }

  // H2: curriculum and teacher conflicts, counted per clashing pair within a period.
  for (int t = 0; t < P; ++t) {
    const std::vector<int>& here = lecturesInPeriod[t];
    for (size_t i = 0; i < here.size(); ++i)
      for (size_t j = i + 1; j < here.size(); ++j) {
        const int ci = m.courseOfClass[here[i]], cj = m.courseOfClass[here[j]];
        if (ci == cj) continue;   // counted by H1
        if (p.courses[ci].teacher == p.courses[cj].teacher) { ++sc.h2; continue; }
        bool shared = false;
        for (const Curriculum& q : p.curricula) {
          const bool hasI = std::find(q.courses.begin(), q.courses.end(), ci) != q.courses.end();
          if (!hasI) continue;
          if (std::find(q.courses.begin(), q.courses.end(), cj) != q.courses.end()) {
            shared = true;
            break;
          }
        }
        if (shared) ++sc.h2;
      }
  }

  // H3: two lectures in one room in one period.
  for (int t = 0; t < P; ++t) {
    std::vector<int> rooms;
    for (int c : lecturesInPeriod[t])
      if (placements[c].room >= 0) rooms.push_back(placements[c].room);
    std::sort(rooms.begin(), rooms.end());
    for (size_t i = 1; i < rooms.size(); ++i)
      if (rooms[i] == rooms[i - 1]) ++sc.h3;
  }

  // S1 RoomCapacity.
  for (size_t c = 0; c < placements.size() && c < m.courseOfClass.size(); ++c) {
    const Placement& pl = placements[c];
    if (!pl.placed() || pl.room < 0) continue;
    const int over = p.courses[m.courseOfClass[c]].students - p.roomCapacity[pl.room];
    if (over > 0) sc.s1 += over;
  }

  // S2 MinimumWorkingDays, S4 RoomStability.
  for (int c = 0; c < nC; ++c) {
    std::set<int> days, rooms;
    for (int period : periodsOfCourse[c]) days.insert(period / p.periodsPerDay);
    for (int r : roomsOfCourse[c])
      if (r >= 0) rooms.insert(r);
    sc.s2 += std::max(0, p.courses[c].minWorkingDays - static_cast<int>(days.size()));
    sc.s4 += rooms.empty() ? 0 : static_cast<int>(rooms.size()) - 1;
  }

  // S3 CurriculumCompactness: per curriculum, per lecture of it, is there an adjacent lecture of
  // the same curriculum on the same day?
  for (const Curriculum& q : p.curricula) {
    std::vector<int> count(P, 0);
    for (int c : q.courses)
      for (int period : periodsOfCourse[c]) ++count[period];
    for (int d = 0; d < p.days; ++d)
      for (int t = 0; t < p.periodsPerDay; ++t) {
        const int period = d * p.periodsPerDay + t;
        if (count[period] == 0) continue;
        const bool left = t > 0 && count[period - 1] > 0;
        const bool right = t + 1 < p.periodsPerDay && count[period + 1] > 0;
        if (!left && !right) sc.s3 += count[period];
      }
  }
  return sc;
}

}  // namespace ctt
}  // namespace tt
