#include "benchmark/Benchmark.h"

#include "benchmark/MemoryUsage.h"
#include "config/Config.h"
#include "scenarios/PlainBattle.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace wargame {

namespace {

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double mean(const std::vector<double>& v) {
    if (v.empty()) {
        return 0.0;
    }
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

// Sample standard deviation (n-1). 0 for fewer than two values: one point has
// no spread to report, not an undefined one.
double stddev(const std::vector<double>& v) {
    if (v.size() < 2) {
        return 0.0;
    }
    const double m = mean(v);
    double sumSq = 0.0;
    for (const double x : v) {
        sumSq += (x - m) * (x - m);
    }
    return std::sqrt(sumSq / static_cast<double>(v.size() - 1));
}

std::ofstream openCsv(const std::string& path) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path);
    if (!out) {
        throw ConfigError("cannot open benchmark output file " + path);
    }
    return out;
}

// Per-window throughput (events/s) derived from the cumulative RunSamples of
// one run, in the order they were taken.
std::vector<double> windowEventsPerSecond(const std::vector<RunSample>& samples) {
    std::vector<double> out;
    std::uint64_t prevEvents = 0;
    double prevWall = 0.0;
    for (const RunSample& s : samples) {
        const double dWall = s.wallSeconds - prevWall;
        const auto dEvents = s.events - prevEvents;
        out.push_back(dWall > 0.0 ? static_cast<double>(dEvents) / dWall : 0.0);
        prevEvents = s.events;
        prevWall = s.wallSeconds;
    }
    return out;
}

// Drops the first and last window (deployment, and the stretch that ends the
// battle) and returns the median of what is left. With too few windows to
// drop anything from, falls back to the median of all of them: a short run is
// not a reason to report nothing.
double steadyStateEventsPerSecond(const std::vector<RunSample>& samples) {
    std::vector<double> windows = windowEventsPerSecond(samples);
    if (windows.size() <= 2) {
        return median(windows);
    }
    return median(std::vector<double>(windows.begin() + 1, windows.end() - 1));
}

}  // namespace

StopKind classifyStop(const std::string& stopReason) {
    if (stopReason.find("eliminated") != std::string::npos) {
        return StopKind::Decided;
    }
    if (stopReason.find("stalemate") != std::string::npos) {
        return StopKind::Stalemate;
    }
    return StopKind::MissionWindow;
}

BenchRun measureOnce(const Config& cfg, std::uint64_t sampleEveryEvents) {
    Simulation sim;
    sim.init(cfg);
    sim.setSampleEveryEvents(sampleEveryEvents);

    PlainBattle battle;
    battle.build(sim, cfg);

    const RunStats stats = sim.run();

    BenchRun r;
    r.unitsPerSide = battle.perSide();
    r.objects = sim.entityCount();
    r.worldSide = sim.world().cols();
    r.seed = cfg.req<std::uint64_t>("sim.seed");
    r.events = stats.eventsProcessed;
    r.dispatchCalls = stats.dispatchCalls;
    r.endTick = stats.endTick;
    r.wallSeconds = stats.wallSeconds;
    r.checksum = stats.checksum;
    r.stopReason = stats.stopReason;
    r.samples = sim.samples();
    return r;
}

bool runBenchmark(const Config& cfg) {
    if (cfg.req<bool>("benchmark.state_dump_enabled")) {
        throw ConfigError(
            "benchmark.state_dump_enabled must be false while measuring: writing "
            "state inside the loop would make the timing meaningless");
    }

    const auto scales = cfg.req<std::vector<std::int32_t>>(
        "benchmark.unit_counts_per_side");
    const auto worldSides = cfg.req<std::vector<std::int32_t>>(
        "benchmark.world_sides");
    if (scales.empty() || scales.size() != worldSides.size()) {
        throw ConfigError(
            "benchmark.unit_counts_per_side and benchmark.world_sides must be "
            "lists of the same, non-zero length");
    }
    const auto seedsPerScale = cfg.req<std::int32_t>("benchmark.seeds_per_scale");
    const auto warmups = cfg.req<std::int32_t>("benchmark.warmup_runs");
    const auto window = cfg.req<std::uint64_t>("benchmark.report_every_events");
    if (seedsPerScale < 1 || warmups < 0 || window < 1) {
        throw ConfigError(
            "benchmark.seeds_per_scale must be >= 1, warmup_runs >= 0 and "
            "report_every_events >= 1");
    }
    const auto baseSeed = cfg.req<std::uint64_t>("sim.seed");

    // The steady-state figure (see steadyStateEventsPerSecond) is sampled from
    // several seeds, not just one: a single seed is one battle's worth of
    // noise, not a figure to build a claim on. Sampling every seed would make
    // the loop pay for window timestamps it does not need, so it is capped.
    const std::int32_t kSteadySeeds = std::min<std::int32_t>(5, seedsPerScale);

    std::ofstream windows = openCsv(cfg.req<std::string>("benchmark.output_csv"));
    std::ofstream summary = openCsv(cfg.req<std::string>("benchmark.summary_csv"));

    // One row per timing window, but ONLY for the first seed of each size (the
    // "representative run"): with 20 seeds, logging every window of every seed
    // would make this file enormous for no benefit — the summary already
    // carries the across-seed spread.
    windows << "units_per_side,objects,seed,window,events_end,tick_end,"
               "wall_s_end,window_events,window_ticks,window_wall_s,"
               "events_per_s,ticks_per_s\n";
    summary << "units_per_side,objects,world_side,seeds_measured,decided,"
               "stalemate,mission_window,median_wall_s,stddev_wall_s,"
               "median_events_per_s,stddev_events_per_s,median_ticks_per_s,"
               "steady_events_per_s,stddev_steady_events_per_s,steady_seeds,"
               "peak_working_set_bytes,added_working_set_bytes,"
               "representative_seed,representative_checksum\n";

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  " << std::setw(10) << "units/side" << std::setw(9) << "objects"
              << std::setw(7) << "seeds" << std::setw(10) << "decided"
              << std::setw(11) << "stalemate" << std::setw(9) << "window"
              << std::setw(12) << "median s" << std::setw(14) << "events/s"
              << std::setw(14) << "ticks/s" << std::setw(14) << "steady ev/s"
              << std::setw(11) << "peak MB" << std::setw(10) << "+MB"
              << "\n";

    bool sane = true;

    for (size_t s = 0; s < scales.size(); ++s) {
        Config scaled = cfg;
        scaled.set("scale.pilot_units_per_side", scales[s]);
        scaled.set("world.rows", worldSides[s]);
        scaled.set("world.cols", worldSides[s]);

        // Peak working set only ever rises for the life of the process, so the
        // reading just before this size's runs is the high-water mark left by
        // every smaller size that ran before it (their own objects are long
        // since freed). The difference after is what THIS size added.
        const std::uint64_t peakBeforeBytes = peakWorkingSetBytes();

        for (std::int32_t w = 0; w < warmups; ++w) {
            measureOnce(scaled, 0);
        }

        std::vector<BenchRun> runs;
        runs.reserve(static_cast<size_t>(seedsPerScale));
        for (std::int32_t i = 0; i < seedsPerScale; ++i) {
            Config perSeed = scaled;
            const std::uint64_t seed = baseSeed + static_cast<std::uint64_t>(i);
            perSeed.set("sim.seed", seed);
            // The first kSteadySeeds seeds are sampled window-by-window, for
            // the steady-state figure; only seed[0] of those is also written
            // to the windows CSV (see below).
            const std::uint64_t sampleEvery = (i < kSteadySeeds) ? window : 0;
            runs.push_back(measureOnce(perSeed, sampleEvery));
        }

        std::int32_t decided = 0, stalemate = 0, missionWindow = 0;
        std::vector<double> walls, eps, tps;
        for (const BenchRun& r : runs) {
            if (r.events == 0) {
                sane = false;
            }
            switch (classifyStop(r.stopReason)) {
                case StopKind::Decided:
                    ++decided;
                    break;
                case StopKind::Stalemate:
                    ++stalemate;
                    break;
                case StopKind::MissionWindow:
                    ++missionWindow;
                    break;
            }
            walls.push_back(r.wallSeconds);
            eps.push_back(r.eventsPerSecond());
            tps.push_back(r.ticksPerSecond());
        }

        const double medWall = median(walls);
        const double sdWall = stddev(walls);
        const double medEps = median(eps);
        const double sdEps = stddev(eps);
        const double medTps = median(tps);
        const BenchRun& rep = runs.front();  // seed[0]: the one the windows CSV is from

        std::vector<double> steadyPerSeed;
        for (std::int32_t i = 0; i < kSteadySeeds; ++i) {
            if (!runs[static_cast<size_t>(i)].samples.empty()) {
                steadyPerSeed.push_back(
                    steadyStateEventsPerSecond(runs[static_cast<size_t>(i)].samples));
            }
        }
        const double steadyEps = median(steadyPerSeed);
        const double steadySdEps = stddev(steadyPerSeed);

        const std::uint64_t peakAfterBytes = peakWorkingSetBytes();
        const std::uint64_t addedBytes =
            peakAfterBytes > peakBeforeBytes ? peakAfterBytes - peakBeforeBytes : 0;

        // windows CSV, representative run only
        std::uint64_t prevEvents = 0;
        Tick prevTick = 0;
        double prevWall = 0.0;
        std::int32_t index = 0;
        for (const RunSample& sample : rep.samples) {
            const double dWall = sample.wallSeconds - prevWall;
            const auto dEvents = sample.events - prevEvents;
            const auto dTicks = sample.tick - prevTick;
            windows << rep.unitsPerSide << ',' << rep.objects << ',' << rep.seed
                    << ',' << ++index << ',' << sample.events << ',' << sample.tick
                    << ',' << sample.wallSeconds << ',' << dEvents << ',' << dTicks
                    << ',' << dWall << ','
                    << (dWall > 0.0 ? static_cast<double>(dEvents) / dWall : 0.0)
                    << ','
                    << (dWall > 0.0 ? static_cast<double>(dTicks) / dWall : 0.0)
                    << '\n';
            prevEvents = sample.events;
            prevTick = sample.tick;
            prevWall = sample.wallSeconds;
        }

        summary << rep.unitsPerSide << ',' << rep.objects << ',' << rep.worldSide
                << ',' << seedsPerScale << ',' << decided << ',' << stalemate << ','
                << missionWindow << ',' << medWall << ',' << sdWall << ',' << medEps
                << ',' << sdEps << ',' << medTps << ',' << steadyEps << ','
                << steadySdEps << ',' << steadyPerSeed.size() << ',' << peakAfterBytes
                << ',' << addedBytes << ',' << rep.seed << ",0x" << std::hex
                << rep.checksum << std::dec << '\n';

        std::cout << "  " << std::setw(10) << rep.unitsPerSide << std::setw(9)
                  << rep.objects << std::setw(7) << seedsPerScale << std::setw(10)
                  << decided << std::setw(11) << stalemate << std::setw(9)
                  << missionWindow << std::setw(12) << medWall << std::setw(14)
                  << std::setprecision(0) << medEps << std::setw(14) << medTps
                  << std::setw(14) << steadyEps << std::setprecision(1)
                  << std::setw(11) << (static_cast<double>(peakAfterBytes) / 1e6)
                  << std::setw(10) << (static_cast<double>(addedBytes) / 1e6)
                  << std::setprecision(4) << "\n";
    }

    std::cout << "\n  (\"decided\"/\"stalemate\" count how the " << scales.size()
              << " sizes' seeds ended: sizes that mostly stalemate are not doing\n"
              << "   comparable work to sizes that mostly decide by elimination.)\n"
              << "  (\"ticks/s\" is simulated time reached / wall time for THIS battle: it\n"
              << "   falls at a size whose battles simply run longer in simulated time, even\n"
              << "   if the engine costs exactly the same per event. \"events/s\" is the\n"
              << "   engine-cost figure that is safe to compare across sizes; \"steady ev/s\"\n"
              << "   is events/s with the opening deployment and the battle's last stretch\n"
              << "   left out, averaged over " << kSteadySeeds << " seeds.)\n"
              << "  (\"peak MB\" is the process's peak working set so far, which the OS "
                 "only ever\n"
              << "   raises; \"+MB\" is how much of that this size added over the "
                 "sizes before it.)\n"
              << "  windows: " << cfg.req<std::string>("benchmark.output_csv")
              << " (seed[0] of each size only)\n"
              << "  summary: " << cfg.req<std::string>("benchmark.summary_csv")
              << "\n";
    return sane;
}

}  // namespace wargame
