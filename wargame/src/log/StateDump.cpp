#include "log/StateDump.h"

#include "config/Config.h"
#include "engine/Simulation.h"
#include "scenarios/PlainBattle.h"

#include <filesystem>
#include <iomanip>

namespace wargame {

StateDump::StateDump(const PlainBattle& battle, const std::string& path,
                     Tick interval)
    : battle_(battle), path_(path) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    out_.open(path);
    if (!out_) {
        throw ConfigError("cannot open state dump file " + path);
    }
    setFrameInterval(interval < 1 ? 1 : interval);
}

void StateDump::begin(const Simulation& sim) {
    out_ << "tick,day,side,id,row,col,state,hp,hp_pct,munitions,fuel,supplies\n";
    out_ << std::fixed << std::setprecision(2);
    writeAll(sim);
}

void StateDump::frame(const Simulation& sim) { writeAll(sim); }

void StateDump::end(const Simulation& sim) {
    writeAll(sim);
    out_.flush();
}

void StateDump::writeAll(const Simulation& sim) {
    const double day = sim.ticksToSeconds(sim.now()) / 86400.0;
    for (const Side side : {Side::Blue, Side::Red}) {
        for (const InfantryBattalion* u : battle_.sideUnits(side)) {
            out_ << sim.now() << ',' << day << ',' << sideName(side) << ','
                 << u->id() << ',' << u->loc().row << ',' << u->loc().col << ','
                 << (u->alive() ? InfantryBattalion::stateName(u->state()) : "dead")
                 << ',' << u->hp() << ',' << 100.0 * u->capabilityFactor() << ','
                 << u->amount(Resource::Munitions) << ','
                 << u->amount(Resource::Fuel) << ','
                 << u->amount(Resource::Supplies) << '\n';
            ++rows_;
        }
    }
}

}  // namespace wargame
