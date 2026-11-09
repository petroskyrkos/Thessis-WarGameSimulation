// Checks of the pilot battle: that it happens, ends sensibly, is reproducible,
// and that attaching a viewer or a log does not change it.

#include "config/Config.h"
#include "engine/Simulation.h"
#include "log/EventLog.h"
#include "scenarios/PlainBattle.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"
#include "viewer/Viewer.h"

#include <iostream>
#include <map>
#include <string>

namespace wargame::selfcheck {

namespace {

// Checks, at every frame of a run, that the occupancy index still agrees with
// the units themselves. A unit must appear in exactly one box, and the counts
// the map is drawn from must equal the number of units actually alive.
//
// Without this, a bookkeeping slip would show up only as a number flickering on
// screen, with no way to tell a display artefact from a corrupted index.
class ConsistencyProbe : public wargame::Viewer {
  public:
    void begin(const wargame::Simulation& s) override { check(s); }
    void frame(const wargame::Simulation& s) override { check(s); }
    void end(const wargame::Simulation& s) override { check(s); }

    std::int64_t framesChecked = 0;
    std::int64_t mismatches = 0;

  private:
    void check(const wargame::Simulation& sim) {
        const wargame::World& w = sim.world();
        if (w.sideCount(wargame::Side::Blue) + w.sideCount(wargame::Side::Red) == 0) {
            return;  // before the units have registered themselves
        }
        ++framesChecked;

        std::int32_t cellBlue = 0;
        std::int32_t cellRed = 0;
        for (std::int32_t i = 0; i < w.cellCount(); ++i) {
            cellBlue += w.at(i).blueCount;
            cellRed += w.at(i).redCount;
        }

        std::map<std::int32_t, int> appearances;
        for (std::int32_t i = 0; i < w.cellCount(); ++i) {
            w.forEachOccupant(w.locOf(i),
                              [&](std::int32_t id) { ++appearances[id]; });
        }

        std::int32_t aliveBlue = 0;
        std::int32_t aliveRed = 0;
        auto& mutableSim = const_cast<wargame::Simulation&>(sim);
        for (std::int32_t id = 0; id < sim.entityCount(); ++id) {
            const wargame::Entity* e = mutableSim.entity(id);
            if (e == nullptr || !e->alive()) {
                continue;
            }
            if (e->side() == wargame::Side::Blue) {
                ++aliveBlue;
            } else {
                ++aliveRed;
            }
        }

        bool duplicated = false;
        for (const auto& [id, n] : appearances) {
            (void)id;
            if (n > 1) {
                duplicated = true;
                break;
            }
        }

        if (cellBlue != aliveBlue || cellRed != aliveRed || duplicated ||
            w.sideCount(wargame::Side::Blue) != aliveBlue ||
            w.sideCount(wargame::Side::Red) != aliveRed) {
            ++mismatches;
        }
    }
};

struct BattleOutcome {
    wargame::RunStats stats;
    wargame::SideReport blue;
    wargame::SideReport red;
};

BattleOutcome runBattle(const wargame::Config& cfg, std::string* printed) {
    wargame::Simulation sim;
    sim.init(cfg);

    wargame::PlainBattle battle;
    battle.build(sim, cfg);

    BattleOutcome out;
    out.stats = sim.run();
    out.blue = battle.report(wargame::Side::Blue);
    out.red = battle.report(wargame::Side::Red);

    if (printed != nullptr) {
        *printed = battle.summary(sim) + "\n" + battle.map(sim, cfg.req<std::int32_t>("viz.console_max_width"));
    }
    return out;
}

}  // namespace

void runBattleChecks(const wargame::Config& cfg) {
    std::string printed;
    const BattleOutcome a = runBattle(cfg, &printed);
    const BattleOutcome b = runBattle(cfg, nullptr);

    const auto perSide = cfg.req<std::int32_t>("scale.pilot_units_per_side");

    report("both sides start at full strength",
           a.blue.total == perSide && a.red.total == perSide);
    report("the battle actually happened",
           a.blue.shots > 0 && a.red.shots > 0);
    report("units were destroyed",
           a.blue.kills > 0 && a.red.kills > 0);
    report("kills on one side match losses on the other",
           a.blue.kills == (a.red.total - a.red.alive) &&
               a.red.kills == (a.blue.total - a.blue.alive));
    report("units moved across the map",
           a.blue.boxesMoved > 0 && a.red.boxesMoved > 0);

    // Each unit should have exactly one event pending at any moment. If a code
    // path ever schedules twice, the queue doubles on every decision and the
    // run dies of exhaustion — this catches that as a failed check instead.
    report("the event queue stays proportional to the force",
           a.stats.maxQueueSize <= static_cast<std::uint64_t>(2 * perSide) + 16);
    report("supplies were consumed over the campaign",
           a.blue.meanSuppliesFraction < 1.0);
    report("the run finished within the mission window",
           a.stats.endTick <= cfg.req<double>("sim.mission_days") * 86400.0 /
                                  cfg.req<double>("sim.tick_seconds") +
                              1.0);

    // Replay the same battle with the probe attached. It is a viewer, so the
    // engine treats it exactly like the live window — and the run must come out
    // identical.
    {
        wargame::Simulation sim;
        sim.init(cfg);
        wargame::PlainBattle probeBattle;
        probeBattle.build(sim, cfg);

        ConsistencyProbe probe;
        probe.setFrameInterval(1);  // every tick that carries an event
        probe.setSpeed(0.0);        // no playback delay
        sim.setViewer(&probe);
        const wargame::RunStats s = sim.run();

        report("the occupancy index matches the living units at every frame",
               probe.framesChecked > 0 && probe.mismatches == 0);
        report("attaching a viewer does not change the run",
               s.checksum == a.stats.checksum);
    }

    // Logging is output only. If writing a record of the battle changed the
    // battle, the record would be worthless.
    {
        wargame::Simulation sim;
        sim.init(cfg);
        wargame::PlainBattle logged;
        logged.build(sim, cfg);

        wargame::EventLog log;
        const bool opened = log.open(cfg, true);
        if (opened) {
            log.setSoldiersAtFullStrength(cfg.req<double>(
                "units.infantry_battalion.soldiers_at_full_strength"));
            log.setSitrepInterval(sim.secondsToTicks(
                60.0 * cfg.req<double>("log.sitrep_interval_sim_minutes")));
        }
        sim.setLog(opened ? &log : nullptr);
        const wargame::RunStats s = sim.run();
        const std::uint64_t anomalies = log.anomalies();
        log.close();

        report("writing the battle log does not change the battle",
               s.checksum == a.stats.checksum);
        report("the log recorded something", log.lines() > 0);
        report("the audit found no broken invariants", anomalies == 0);
    }

    report("a full battle is reproducible from its seed",
           a.stats.checksum == b.stats.checksum &&
               a.blue.alive == b.blue.alive && a.red.alive == b.red.alive &&
               a.blue.shots == b.blue.shots);

    std::cout << "\npilot battle (" << perSide
              << " infantry battalions a side, flat ground):\n"
              << printed << "\n  " << a.stats.str() << "\n";
}

}  // namespace wargame::selfcheck
