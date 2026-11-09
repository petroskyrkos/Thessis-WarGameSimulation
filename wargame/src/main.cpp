// War game simulation — entry point.
//
// PHASE 1: classical (monolithic) forces. Each platform carries its own full
// set of capabilities, exactly as a conventional order of battle does. There is
// no Mosaic code in this build and none is planned until phase 1 is complete
// and validated — see CLAUDE.md.
//
// Run with no flags: load the config, run the self-checks, report what is still
// unresearched. With --live and/or --log: run one pilot battle, watched and/or
// recorded.

// Debug-CRT leak check (MSVC only, and only in a Debug build — _DEBUG is never
// defined for the Release build this program is measured with). Reports the
// size and count of every heap block still allocated when the process exits,
// as "Detected memory leaks!" on stderr/the debugger output. Zero cost and
// zero effect on the Release binary: see docs/CODE_ANALYSIS.txt §5
// ("Μνήμη / leaks") for how to read the result and for the separate
// AddressSanitizer build that checks heap corruption instead of leaks.
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "config/Config.h"
#include "benchmark/Benchmark.h"
#include "engine/Simulation.h"
#include "log/EventLog.h"
#include "log/StateDump.h"
#include "scenarios/PlainBattle.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"
#include "viewer/Viewer.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

// Reads one line and asks `parse` to turn it into a T. An empty line (just
// Enter) always keeps `configured`; a line `parse` rejects asks again. If
// stdin closes first (e.g. piped input ran out), falls back to `configured`
// rather than looping forever.
template <typename T, typename Parse>
T promptForValue(const std::string& label, T configured, Parse parse) {
    std::cout << label << " [Enter = " << configured << "]: ";
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            return configured;
        }
        T value{};
        if (parse(line, value)) {
            return value;
        }
        std::cout << "  not understood, try again (or Enter for " << configured
                  << "): ";
    }
    return configured;
}

// Parses `line` as a whole number of type T with std::stoXXX-style semantics:
// the ENTIRE line must be consumed (no trailing junk), and the result must
// satisfy `ok`. Used by the three prompts below.
template <typename T, typename Convert, typename Ok>
bool parseWholeLine(const std::string& line, T& out, Convert convert, Ok ok) {
    try {
        size_t consumed = 0;
        const auto value = convert(line, &consumed);
        if (consumed != line.size() || !ok(value)) {
            return false;
        }
        out = static_cast<T>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// These four are the only values asked about before --live/--log/--dump-state
// starts: the ones worth changing on a whim, without editing the config, to
// look at a different battle. Grid size comes first, because whether a given
// units-per-side fits depends on it. Never called for the self-checks or for
// --symmetry/--benchmark: those run many battles unattended and must never
// wait on a human.
std::int32_t promptForGridSide(std::int32_t configured) {
    return promptForValue<std::int32_t>(
        "grid size, N for an NxN world of boxes", configured,
        [](const std::string& line, std::int32_t& out) {
            return parseWholeLine(
                line, out,
                [](const std::string& s, size_t* pos) { return std::stoi(s, pos); },
                [](int v) { return v > 0; });
        });
}

std::uint64_t promptForSeed(std::uint64_t configured) {
    return promptForValue<std::uint64_t>(
        "seed", configured, [](const std::string& line, std::uint64_t& out) {
            return parseWholeLine(
                line, out,
                [](const std::string& s, size_t* pos) {
                    return std::stoull(s, pos);
                },
                [](unsigned long long) { return true; });
        });
}

std::int32_t promptForUnitsPerSide(std::int32_t configured) {
    return promptForValue<std::int32_t>(
        "units per side (must fit the grid)", configured,
        [](const std::string& line, std::int32_t& out) {
            return parseWholeLine(
                line, out,
                [](const std::string& s, size_t* pos) { return std::stoi(s, pos); },
                [](int v) { return v > 0; });
        });
}

double promptForPlaybackSpeed(double configured) {
    return promptForValue<double>(
        "playback speed, sim hours per real second (0 = as fast as possible)",
        configured, [](const std::string& line, double& out) {
            return parseWholeLine(
                line, out,
                [](const std::string& s, size_t* pos) { return std::stod(s, pos); },
                [](double v) { return v >= 0.0; });
        });
}

// Runs one battle, optionally watched and optionally logged.
int runSingleBattle(const wargame::Config& cfg, bool live, bool logging,
                    bool dumping, std::int32_t gridSide, std::uint64_t seed,
                    std::int32_t unitsPerSide, double playbackSpeed) {
    // A local copy: the four prompted values apply only to this run, never
    // written back to the config file.
    wargame::Config runCfg = cfg;
    runCfg.set("world.rows", gridSide);
    runCfg.set("world.cols", gridSide);
    runCfg.set("scale.pilot_units_per_side", unitsPerSide);
    if (live) {
        runCfg.set("viz.sim_hours_per_real_second", playbackSpeed);
    }

    wargame::Simulation sim;
    sim.init(runCfg);
    sim.reseed(seed);
    std::cout << "running with seed " << seed << ", " << unitsPerSide
              << " units per side, " << gridSide << "x" << gridSide
              << " grid\n";

    wargame::PlainBattle battle;
    battle.build(sim, runCfg);

    // The state dump is a viewer too, so it cannot be combined with --live.
    std::unique_ptr<wargame::Viewer> viewer;
    if (live) {
        viewer = wargame::makeViewer(runCfg, sim, true);
        sim.setViewer(viewer.get());
    } else if (dumping) {
        const std::string path = runCfg.req<std::string>("log.directory") + "/" +
                                 runCfg.req<std::string>("log.file_stem") +
                                 "_state.csv";
        viewer = std::make_unique<wargame::StateDump>(
            battle, path,
            sim.secondsToTicks(
                60.0 *
                runCfg.req<double>("benchmark.state_dump_interval_sim_minutes")));
    }
    if (viewer && !live) {
        sim.setViewer(viewer.get());
    }

    const auto* dump = dynamic_cast<wargame::StateDump*>(viewer.get());

    wargame::EventLog log;
    if (logging && log.open(runCfg, true)) {
        log.setSoldiersAtFullStrength(runCfg.req<double>(
            "units.infantry_battalion.soldiers_at_full_strength"));
        log.setSitrepInterval(sim.secondsToTicks(
            60.0 * runCfg.req<double>("log.sitrep_interval_sim_minutes")));
        sim.setLog(&log);
    }

    const wargame::RunStats stats = sim.run();

    std::cout << "\n" << battle.summary(sim) << "\n  " << stats.str() << "\n";
    if (dump != nullptr) {
        std::cout << "\n  state dump: " << dump->rows() << " rows\n    "
                  << dump->path() << "\n";
    }
    if (log.enabled()) {
        log.close();
        std::cout << "\n  battle log: " << log.lines() << " entries, "
                  << log.anomalies() << " anomalies\n";
        if (!log.csvPath().empty()) {
            std::cout << "    " << log.csvPath() << "\n";
        }
        if (!log.textPath().empty()) {
            std::cout << "    " << log.textPath() << "\n";
        }
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
    // Every block still allocated at exit gets dumped to stderr as
    // "Detected memory leaks!" with file/line where it was allocated.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    std::cout << "War game simulation core\n"
              << "Phase 1: CLASSICAL (monolithic) forces - Model 0\n"
              << "Mosaic concept: not in this build (phase 3)\n\n";

    std::string configPath = "config/sim.json";
    bool live = false;
    bool logging = false;
    bool symmetry = false;
    bool benchmark = false;
    bool dumping = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--live") {
            live = true;
        } else if (arg == "--log") {
            logging = true;
        } else if (arg == "--symmetry") {
            symmetry = true;
        } else if (arg == "--benchmark") {
            benchmark = true;
        } else if (arg == "--dump-state") {
            dumping = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "usage: wargame [config.json] [--live] [--log] "
                         "[--symmetry] [--benchmark] [--dump-state]\n"
                      << "  --live      watch one battle instead of running the "
                         "self-checks\n"
                      << "  --log       write a full record of the battle to "
                         "results/\n"
                      << "  --symmetry  run the battle over many seeds and test "
                         "that no side is favoured\n"
                      << "  --benchmark  run the battle at several sizes and time the "
                         "engine (benchmark.* in the config)\n"
                      << "  --dump-state write every unit's state at intervals to "
                         "results/ (one battle; not with --live)\n"
                      << "  --live/--log/--dump-state ask for a grid size, seed, "
                         "units per side, and (for --live) playback\n"
                      << "                speed at the start — Enter on any of "
                         "them keeps the config's own value\n";
            return EXIT_SUCCESS;
        } else {
            configPath = arg;
        }
    }

    wargame::Config cfg;
    wargame::Simulation sim;

    try {
        cfg.loadFile(configPath);
        std::cout << "config:  " << cfg.source() << "\n";

        sim.init(cfg);

        std::cout << "world:   " << sim.world().summary() << "\n"
                  << "tick:    " << sim.tickSeconds() << " s, run ends at tick "
                  << sim.endOfRun() << " ("
                  << cfg.req<double>("sim.mission_days") << " days)\n"
                  << "pilot:   " << cfg.req<int>("scale.pilot_units_per_side")
                  << " infantry battalions per side\n"
                  << "seed:    " << cfg.req<std::uint64_t>("sim.seed") << "\n\n";
    } catch (const wargame::ConfigError& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    if (symmetry) {
        try {
            std::cout << "symmetry check:\n";
            return wargame::selfcheck::runSymmetryCheck(cfg) ? EXIT_SUCCESS
                                                             : EXIT_FAILURE;
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    if (benchmark) {
        try {
            std::cout << "benchmark:\n";
            return wargame::runBenchmark(cfg) ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    if (!live) {
        dumping = dumping || cfg.req<bool>("benchmark.state_dump_enabled");
    }

    if (live && dumping) {
        std::cerr << "[FATAL] --dump-state cannot be combined with --live\n";
        return EXIT_FAILURE;
    }

    if (live || logging || dumping) {
        try {
            const std::int32_t gridSide =
                promptForGridSide(cfg.req<std::int32_t>("world.rows"));
            const std::uint64_t seed =
                promptForSeed(cfg.req<std::uint64_t>("sim.seed"));
            const std::int32_t unitsPerSide = promptForUnitsPerSide(
                cfg.req<std::int32_t>("scale.pilot_units_per_side"));
            double playbackSpeed = cfg.req<double>("viz.sim_hours_per_real_second");
            if (live) {
                playbackSpeed = promptForPlaybackSpeed(playbackSpeed);
            }
            return runSingleBattle(cfg, live, logging, dumping, gridSide, seed,
                                   unitsPerSide, playbackSpeed);
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "grid checks:\n";
    wargame::selfcheck::runGridChecks(sim.world());

    std::cout << "\nengine checks:\n";
    try {
        wargame::selfcheck::runEngineChecks(cfg);
        std::cout << "\nunit and resource checks:\n";
        wargame::selfcheck::runUnitChecks(cfg);
        std::cout << "\npilot battle checks:\n";
        wargame::selfcheck::runBattleChecks(cfg);
        std::cout << "\nengagement checks (hit probability, electronic warfare):\n";
        wargame::selfcheck::runEngagementChecks(cfg);
        std::cout << "\nbenchmark harness checks:\n";
        wargame::selfcheck::runBenchmarkChecks(cfg);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] engine check raised: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    const auto todos = cfg.todoPaths();
    std::cout << "\n";
    if (todos.empty()) {
        std::cout << "no placeholder values left in the config.\n";
    } else {
        std::cout << "figures still to research (" << todos.size() << "):\n";
        for (const std::string& path : todos) {
            std::cout << "  - " << path << "\n";
        }
    }

    return wargame::selfcheck::allPassed() ? EXIT_SUCCESS : EXIT_FAILURE;
}
