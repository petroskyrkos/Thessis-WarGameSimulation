// The benchmark harness (Step 7).
//
// Answers the supervisor's question: how fast does the engine run at 200
// objects, and what does it look like at 2,000? It runs the pilot battle at
// several sizes. At each size it runs MANY DIFFERENT SEEDS, not repeats of the
// same one — repeating one seed only remeasures noise (OS scheduling, cache
// state), it does not sample different battles. The result at each size is a
// median and a spread over real battles, plus how each of them ended
// (decided by elimination, or stopped as a stalemate): sizes that end
// differently are not doing comparable work, and the report says so rather
// than hiding it in one average.
//
// The engine is event-driven, so a "tick" with nothing happening costs nothing.
// Throughput is therefore reported two ways: events per second (work actually
// done) and simulated ticks per second (how fast the clock advances — this one
// depends on how long THIS battle's dynamics happen to run in simulated time,
// not only on engine speed, so it is not safe to compare across sizes on its
// own). A third number, the STEADY-STATE rate, drops the first and last window
// of each of a handful of sampled runs (deployment, and the last stretch
// before the battle is decided) and reports the median and spread over them,
// for a cleaner, less one-seed-lucky read on the engine's per-event cost.
//
// A fourth figure, PEAK MEMORY, answers the companion question Step 7 does not
// ask in words but the 2,000-object projection implies: not just how fast, but
// how much RAM. It reads the OS's peak-working-set counter before and after
// each size's runs (see benchmark/MemoryUsage.h) — a counter that only ever
// rises, so the difference is the memory that size's battles actually touched,
// net of smaller sizes run earlier in the same process.
//
// Nothing in a measured run does I/O: no viewer, no battle log, no state dump.
// The only extra work is reading the clock once per window of events, and only
// for the handful of runs per size that are sampled.

#pragma once

#include "engine/Event.h"
#include "engine/Simulation.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wargame {

class Config;

struct BenchRun {
    std::int32_t unitsPerSide = 0;
    std::int32_t objects = 0;       // units on both sides
    std::int32_t worldSide = 0;     // the grid is worldSide x worldSide boxes
    std::uint64_t seed = 0;
    std::uint64_t events = 0;
    std::uint64_t dispatchCalls = 0;
    Tick endTick = 0;
    double wallSeconds = 0.0;
    std::uint64_t checksum = 0;
    std::string stopReason;         // empty = ran to the end of the mission window
    std::vector<RunSample> samples; // only filled when asked to sample this run

    double eventsPerSecond() const {
        return wallSeconds > 0.0 ? static_cast<double>(events) / wallSeconds : 0.0;
    }
    double ticksPerSecond() const {
        return wallSeconds > 0.0 ? static_cast<double>(endTick) / wallSeconds : 0.0;
    }
    double microsecondsPerEvent() const {
        return events > 0 ? 1e6 * wallSeconds / static_cast<double>(events) : 0.0;
    }
};

// How a run ended, grouped into the three shapes a pilot battle can take.
enum class StopKind {
    Decided,        // one side was eliminated
    Stalemate,      // sim.stalemate_after_hours fired
    MissionWindow,  // ran to the end of sim.mission_days without either
};

StopKind classifyStop(const std::string& stopReason);

// Builds and runs the pilot battle once from `cfg` and returns the measurement,
// using whatever sim.seed the config carries. `sampleEveryEvents` of 0 takes no
// samples (cheaper; use it for every run except the one representative run per
// size that the steady-state figure is read from).
BenchRun measureOnce(const Config& cfg, std::uint64_t sampleEveryEvents);

// Runs the whole sweep described by benchmark.* in the config: at each size,
// benchmark.seeds_per_scale distinct seeds (sim.seed, sim.seed + 1, ...), after
// benchmark.warmup_runs untimed warm-up runs. Prints a table and writes the two
// CSV files. Returns false only on a sanity failure (a run that measured no
// events) — it does not compare checksums across seeds, since they are
// different battles by design; determinism of a single seed is proved by the
// startup self-checks instead.
bool runBenchmark(const Config& cfg);

}  // namespace wargame
