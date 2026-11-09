// Events — the messages that drive the simulation.
//
// This is a discrete-event simulation: time does not advance in fixed steps.
// The engine jumps to the time of the next event in the queue, so nothing is
// computed for a unit that has nothing to do. Consumption that accrues with
// time is therefore charged lazily, from the elapsed ticks since a unit was
// last touched, rather than by walking every unit every tick.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace wargame {

// Simulation time, counted in ticks of sim.tick_seconds. 64-bit because a
// 30-day run at 2.35 s per tick is already ~1.1 million ticks, and a longer
// campaign must not be able to overflow it.
using Tick = std::int64_t;

// Reserved destinations.
constexpr std::int32_t kBroadcast = -1;   // every entity
constexpr std::int32_t kSimEngine = -2;   // the engine itself, as a source

enum class EventType : std::int16_t {
    SimStart = 0,   // broadcast once at tick 0
    SimStop = 1,    // broadcast once when the run ends
    Wake = 2,       // "update yourself": the generic self-scheduled tick
    Arrive = 3,     // a unit reached a new box
    Engage = 4,     // a unit opens fire on a target
    Destroyed = 5,  // a unit was destroyed
    Resupply = 6,   // a resupply delivery completed
};

const char* eventTypeName(EventType t);

// A single event. Deliberately a POD with inline arguments: the supervisor's
// own Event carries a std::vector<int>, which means a heap allocation for every
// event created. At the event rates this simulation will reach, that allocation
// dominates. Four int32 arguments cover every use in the model so far.
struct Event {
    Tick time = 0;
    // Creation order, assigned by Simulation::postEvent. It breaks ties between
    // events at the same tick.
    //
    // Without it the order of simultaneous events is whatever the standard
    // library's heap happens to produce, which differs between MSVC and GCC. In
    // a firefight, where many units shoot on the same tick, that order decides
    // who dies first — so the same seed would give different outcomes on
    // different machines. This field is what makes a run reproducible.
    std::uint64_t seq = 0;

    // Tie-break key between events on the same tick, assigned by
    // Simulation::postEvent. In "random" mode (sim.tie_break) it is a draw from
    // the seeded RNG, so no side or unit is systematically served first; in
    // "creation_order" mode it is always 0 and the order is decided by `seq`
    // alone.
    //
    // Why this exists: with creation order alone, whoever happens to be created
    // (or numbered) first acts first whenever two events fall on the same tick.
    // In the pilot battle that gave the army on the right a clear advantage
    // (see docs/CODE_ANALYSIS.txt section 6), purely from how units were numbered.
    std::uint64_t tie = 0;

    std::int32_t src = kSimEngine;
    std::int32_t dest = kBroadcast;

    EventType type = EventType::Wake;
    std::int16_t argCount = 0;
    std::array<std::int32_t, 4> args{};

    Event() = default;
    Event(Tick t, EventType ty, std::int32_t source, std::int32_t destination)
        : time(t), src(source), dest(destination), type(ty) {}

    void addArg(std::int32_t v) {
        if (argCount < static_cast<std::int16_t>(args.size())) {
            args[static_cast<size_t>(argCount++)] = v;
        }
    }

    std::int32_t arg(int i) const { return args[static_cast<size_t>(i)]; }

    std::string str() const;
};

// Orders the future event list: earliest tick first; among events on the same
// tick, the lower tie-break key; and if that is also equal, the one created
// first. The last step keeps the order fully determined, so a run is
// reproducible whatever the tie keys are. std::priority_queue pops the greatest
// element, so this comparator reports "a comes after b".
struct EventLater {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time != b.time) {
            return a.time > b.time;
        }
        if (a.tie != b.tie) {
            return a.tie > b.tie;
        }
        return a.seq > b.seq;
    }
};

}  // namespace wargame
