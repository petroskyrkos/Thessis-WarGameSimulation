// A record of everything that happens in a battle.
//
// What is written here are events of the battle — a unit fired, a unit was hit,
// a unit ran dry — not events of the scheduler. A list of Wake messages says
// nothing about the fight; this says what happened and when, which is what the
// write-up needs as evidence.
//
// Two sinks, either or both:
//   csv   one row per event, for analysis and charts
//   text  one sentence per event, for reading
//
// Off by default. Writing from inside the event loop is exactly the I/O the
// supervisor said to keep out of a measured run, so a benchmark attaches no
// log — and a logged run produces the same checksum as an unlogged one.

#pragma once

#include "engine/Event.h"
#include "world/GridPos.h"
#include "world/Side.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace wargame {

class Config;
class DynamicUnit;
class Entity;
class Simulation;

enum class LogEvent : std::uint8_t {
    RunStart,
    Deployed,
    Moved,
    Shot,       // a round was fired and hit; value = damage dealt
    Missed,     // a round was fired and missed; value1 = hit probability used
    NoAmmo,     // a unit tried to fire with an empty magazine
    Stranded,   // a unit tried to move with no fuel
    Destroyed,
    Pursuing,   // objective reached, heading for the enemy's centre of mass
    RunEnd,
    Situation,
    Anomaly,
};

const char* logEventName(LogEvent e);

class EventLog {
  public:
    ~EventLog();

    // Opens the sinks named in the config. `force` turns logging on whatever
    // log.enabled says, for a --log flag. Returns false if logging is off.
    bool open(const Config& cfg, bool force);

    bool enabled() const { return csv_.is_open() || text_.is_open(); }
    std::uint64_t lines() const { return lines_; }
    const std::string& csvPath() const { return csvPath_; }
    const std::string& textPath() const { return textPath_; }

    // Records one event. `actor` is required; `target` may be null. The two
    // values mean whatever the event type says they mean — damage dealt,
    // remaining health, boxes moved.
    void record(const Simulation& sim, LogEvent type, const Entity& actor,
                const Entity* target = nullptr, double value1 = 0.0,
                double value2 = 0.0);

    // A line that belongs to the run rather than to any unit.
    void note(const Simulation& sim, LogEvent type, const std::string& text);

    // A snapshot of both sides: strength, health, resources and what the units
    // are doing. Written at a fixed interval of simulation time, so the log
    // carries the shape of the battle and not only its individual moments.
    void situation(const Simulation& sim);

    // Health is modelled as a pool; soldiers are how a reader understands it.
    // Presentation only — nothing in the model uses this number.
    void setSoldiersAtFullStrength(double n) { soldiersFull_ = n; }

    Tick sitrepInterval() const { return sitrepTicks_; }
    void setSitrepInterval(Tick t) { sitrepTicks_ = t; }

    // True when every single event should be audited, not just each sitrep.
    bool auditStrict() const { return auditStrict_; }
    void setAuditStrict(bool v) { auditStrict_ = v; }

    // Checks the invariants that must always hold — a side's strength never
    // rises, every unit stands in exactly one box, the three counts of a side
    // agree — and writes an `anomaly` line when one breaks, with the previous
    // and current values. This is what turns "I saw the number go up" into a
    // line in a file with a timestamp.
    //
    // Returns the number of anomalies found so far.
    std::uint64_t audit(const Simulation& sim, const char* after);

    std::uint64_t anomalies() const { return anomalies_; }

    void close();

  private:
    void writeUnitColumns(const DynamicUnit* u);
    std::string soldiersOf(const DynamicUnit* u) const;
    std::string casualtiesOf(double damage, double hpMax) const;

    std::ofstream csv_;
    std::ofstream text_;
    std::string csvPath_;
    std::string textPath_;
    std::uint64_t lines_ = 0;
    double soldiersFull_ = 0.0;
    Tick sitrepTicks_ = 0;

    // Audit state: the last strengths seen, so an increase can be spotted.
    std::int32_t lastBlue_ = -1;
    std::int32_t lastRed_ = -1;
    std::uint64_t anomalies_ = 0;
    bool auditStrict_ = false;
};

}  // namespace wargame
