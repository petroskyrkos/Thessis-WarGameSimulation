// Checks of the benchmark harness: that measuring does not change what is
// measured, and that the 2,000-object scale really runs.

#include "benchmark/Benchmark.h"
#include "benchmark/MemoryUsage.h"
#include "config/Config.h"
#include "engine/Simulation.h"
#include "log/StateDump.h"
#include "scenarios/PlainBattle.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"

#include <filesystem>

namespace wargame::selfcheck {

namespace {

// Runs the pilot battle with a state dump attached and returns the checksum.
std::uint64_t runWithDump(const Config& cfg, const std::string& path,
                          std::uint64_t& rowsOut) {
    Simulation sim;
    sim.init(cfg);

    PlainBattle battle;
    battle.build(sim, cfg);

    StateDump dump(battle, path, sim.secondsToTicks(15.0 * 60.0));
    sim.setViewer(&dump);

    const RunStats stats = sim.run();
    rowsOut = dump.rows();
    return stats.checksum;
}

}  // namespace

void runBenchmarkChecks(const Config& cfg) {
    const BenchRun plain = measureOnce(cfg, 0);
    constexpr std::uint64_t kWindow = 500;
    const BenchRun sampled = measureOnce(cfg, kWindow);

    report("taking timing samples does not change the run",
           plain.checksum == sampled.checksum && plain.events == sampled.events);

    bool ordered = true;
    for (size_t i = 1; i < sampled.samples.size(); ++i) {
        const RunSample& a = sampled.samples[i - 1];
        const RunSample& b = sampled.samples[i];
        ordered = ordered && b.events > a.events && b.tick >= a.tick &&
                  b.wallSeconds >= a.wallSeconds;
    }
    report("one sample per window of events, in order",
           sampled.samples.size() == sampled.events / kWindow && ordered &&
               !sampled.samples.empty());

    const std::string path =
        (std::filesystem::temp_directory_path() / "wargame_state_check.csv").string();
    std::uint64_t rows = 0;
    const std::uint64_t dumped = runWithDump(cfg, path, rows);
    std::filesystem::remove(path);
    report("the state dump writes rows and does not change the run",
           dumped == plain.checksum && rows >= 2ull * 2 * plain.unitsPerSide);

    // The scale the supervisor asked about: 2,000 objects.
    Config big = cfg;
    big.set("scale.pilot_units_per_side", 1000);
    big.set("world.rows", 80);
    big.set("world.cols", 80);
    const BenchRun large = measureOnce(big, 0);
    const BenchRun largeAgain = measureOnce(big, 0);
    report("a 2,000-object battle runs, ends, and repeats exactly",
           large.objects == 2000 && large.events > 0 &&
               large.checksum == largeAgain.checksum &&
               large.events == largeAgain.events);

    bool refused = false;
    try {
        Config dumping = cfg;
        dumping.set("benchmark.state_dump_enabled", true);
        runBenchmark(dumping);
    } catch (const ConfigError&) {
        refused = true;
    }
    report("the benchmark refuses to run with the state dump on", refused);

    bool mismatchRefused = false;
    try {
        Config mismatched = cfg;
        mismatched.set("benchmark.world_sides", {64, 64});  // one short of unit_counts_per_side
        runBenchmark(mismatched);
    } catch (const ConfigError&) {
        mismatchRefused = true;
    }
    report("the benchmark refuses mismatched size lists", mismatchRefused);

    report("a stop reason is classified as decided, stalemate, or mission window",
           classifyStop("Red eliminated") == StopKind::Decided &&
               classifyStop("Blue eliminated") == StopKind::Decided &&
               classifyStop("stalemate: no unit moved or fired for 24.0 hours") ==
                   StopKind::Stalemate &&
               classifyStop("") == StopKind::MissionWindow);

    // The memory counter the benchmark's "peak MB"/"+MB" columns are built on:
    // it must read a plausible value (any running process uses well over 1 MB)
    // and must never go DOWN between two readings, which is the property the
    // benchmark relies on to attribute memory growth to one size and not another.
    const std::uint64_t memBefore = peakWorkingSetBytes();
    const BenchRun forMemory = measureOnce(cfg, 0);
    const std::uint64_t memAfter = peakWorkingSetBytes();
    report("the process memory counter reads a plausible, non-decreasing value",
           memBefore > 1'000'000ull && memAfter >= memBefore &&
               forMemory.events > 0);
}

}  // namespace wargame::selfcheck
