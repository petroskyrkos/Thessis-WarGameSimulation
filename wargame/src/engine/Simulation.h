// The discrete-event engine.
//
// Adapted from the supervisor's DESimulationExperiment: a future event list as
// a priority queue, a table of entities, and a loop that pops the next event
// and dispatches it. The differences from that original are deliberate and
// documented where they occur.

#pragma once

#include "pathfinding/AStar.h"
#include "entities/Entity.h"
#include "engine/Event.h"
#include "util/Rng.h"
#include "world/World.h"

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace wargame {

class Config;
class EventLog;
class Viewer;

struct RunStats {
    Tick endTick = 0;
    std::uint64_t eventsProcessed = 0;
    std::uint64_t eventsPosted = 0;
    std::uint64_t dispatchCalls = 0;  // entity handlers actually invoked
    std::uint64_t maxQueueSize = 0;
    double wallSeconds = 0.0;      // excluding time spent drawing
    double viewerSeconds = 0.0;    // time spent drawing and waiting
    bool stoppedEarly = false;     // the run ended before the mission window
    std::string stopReason;        // why, when it did

    // Order-sensitive hash of every event processed. Two runs of the same
    // scenario with the same seed must produce the same value; if they do not,
    // something in the engine is not deterministic.
    std::uint64_t checksum = 0;

    double eventsPerSecond() const {
        return wallSeconds > 0.0 ? static_cast<double>(eventsProcessed) / wallSeconds
                                 : 0.0;
    }

    std::string str() const;
};

// One reading of the progress counters, taken every sampleEveryEvents events
// (see Simulation::setSampleEveryEvents). Kept in memory and written out after
// the run, so sampling adds no I/O to the loop.
struct RunSample {
    std::uint64_t events = 0;  // events processed so far
    Tick tick = 0;             // simulated time reached
    double wallSeconds = 0.0;  // engine time so far, excluding any viewer
};

class Simulation {
  public:
    Simulation() = default;

    // Reads sim.seed, sim.tick_seconds, sim.mission_days, sim.tie_break and
    // sim.stalemate_after_hours, and builds the world. Must be called before any
    // entity is added.
    void init(const Config& cfg);

    // Replaces the seed read by init(). For running the same scenario over many
    // seeds (the symmetry check) without editing the config. Call before run().
    void reseed(std::uint64_t seed) { rng_ = Rng(seed); }

    const World& world() const { return world_; }
    World& world() { return world_; }
    Rng& rng() { return rng_; }

    // One shared path finder. Its working arrays are sized to the grid and
    // reused, so every unit searching through the same object costs far less
    // than each keeping its own.
    AStar& pathfinder() { return pathfinder_; }

    // Attaches a live viewer, or nullptr for none. A run with no viewer does
    // no I/O at all inside the loop, which is what a benchmark needs; a run
    // with one produces exactly the same events, just more slowly.
    void setViewer(Viewer* v) { viewer_ = v; }

    // Attaches a battle log, or nullptr for none. Like the viewer, it is pure
    // output: a logged run and an unlogged one are the same run.
    void setLog(EventLog* l) { log_ = l; }
    EventLog* log() { return log_; }

    // Ends the run at the current event. Used when the scenario has reached a
    // decision — one side wiped out, say — so neither a measured run nor a
    // watched one carries on through an empty battlefield.
    void requestStop(const std::string& reason);

    // A unit calls this whenever it does something that changes the battle: it
    // sets off to a new box, or fires. If nothing calls it for
    // sim.stalemate_after_hours, the run stops with the reason "stalemate": the
    // survivors can no longer act (typically all out of fuel and out of contact),
    // and carrying on to the end of the mission window would only simulate idling.
    void noteActivity() { lastActivity_ = now_; }

    // True when simultaneous events are ordered by a random key (sim.tie_break).
    bool randomTieBreak() const { return randomTieBreak_; }

    // Overrides sim.tie_break after init(), so a check can compare both modes
    // without editing the config. Call before run().
    void setRandomTieBreak(bool random) { randomTieBreak_ = random; }

    Tick now() const { return now_; }
    Tick endOfRun() const { return endTick_; }
    double tickSeconds() const { return tickSeconds_; }

    // Converts between ticks and wall time, so a rate given per day can be
    // charged per tick.
    double ticksToSeconds(Tick t) const {
        return static_cast<double>(t) * tickSeconds_;
    }
    Tick secondsToTicks(double seconds) const;
    Tick daysToTicks(double days) const { return secondsToTicks(days * 86400.0); }

    // Takes ownership and returns the entity's ID, which is its index in the
    // table.
    std::int32_t addEntity(std::unique_ptr<Entity> e);

    std::int32_t entityCount() const {
        return static_cast<std::int32_t>(entities_.size());
    }
    // Null for an ID that does not exist.
    Entity* entity(std::int32_t id);

    // Queues an event. The engine stamps it with its creation order and, in
    // "random" tie-break mode, a random key drawn from the seeded RNG. Together
    // they decide the order of simultaneous events, deterministically for a given
    // seed.
    //
    // An event scheduled past the end of the run is dropped, exactly as in the
    // original: a unit may plan something that the end of the campaign cuts
    // short.
    bool postEvent(Event e);

    // Runs until the queue empties or the end tick is reached. Does no I/O:
    // writing state from inside this loop would make every timing measurement
    // meaningless.
    RunStats run();

    const RunStats& stats() const { return stats_; }

    // Takes a RunSample every `n` events (0 = never, the default). For the
    // benchmark: the samples give time per window of work. Call before run().
    void setSampleEveryEvents(std::uint64_t n) { sampleEvery_ = n; }
    const std::vector<RunSample>& samples() const { return samples_; }

  private:
    // Delivers one event to its destination, or to every live entity when the
    // destination is kBroadcast.
    void dispatch(const Event& e);

    World world_;
    Rng rng_{0};
    AStar pathfinder_;
    Viewer* viewer_ = nullptr;  // not owned
    EventLog* log_ = nullptr;   // not owned
    bool stopRequested_ = false;
    std::string stopReason_;

    double tickSeconds_ = 0.0;
    Tick now_ = 0;
    Tick endTick_ = 0;
    std::uint64_t nextSeq_ = 0;

    bool randomTieBreak_ = false;  // sim.tie_break
    Tick stalemateTicks_ = 0;      // sim.stalemate_after_hours in ticks; 0 = off
    Tick lastActivity_ = 0;        // tick of the last noteActivity()

    // Entities are held in a flat vector indexed by ID, not in a std::map as in
    // the original. A lookup is then one addition instead of a walk down a
    // red-black tree, and a broadcast is a linear sweep over contiguous memory.
    // With broadcasts reaching ~2,000 entities, this is the engine's hot path.
    std::vector<std::unique_ptr<Entity>> entities_;

    std::priority_queue<Event, std::vector<Event>, EventLater> fev_;

    RunStats stats_;

    std::uint64_t sampleEvery_ = 0;
    std::vector<RunSample> samples_;
};

}  // namespace wargame
