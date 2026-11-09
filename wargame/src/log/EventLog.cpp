#include "log/EventLog.h"

#include "config/Config.h"
#include "entities/Entity.h"
#include "entities/infantry/InfantryBattalion.h"
#include "entities/DynamicUnit.h"
#include "engine/Simulation.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace wargame {

const char* logEventName(LogEvent e) {
    switch (e) {
        case LogEvent::RunStart: return "run_start";
        case LogEvent::Deployed: return "deployed";
        case LogEvent::Moved: return "moved";
        case LogEvent::Shot: return "shot";
        case LogEvent::Missed: return "missed";
        case LogEvent::NoAmmo: return "no_ammo";
        case LogEvent::Stranded: return "stranded";
        case LogEvent::Destroyed: return "destroyed";
        case LogEvent::Pursuing: return "pursuing";
        case LogEvent::RunEnd: return "run_end";
        case LogEvent::Situation: return "situation";
        case LogEvent::Anomaly: return "anomaly";
    }
    return "?";
}

namespace {

// "3d 07:42" — easier to read in a battle narrative than a raw tick.
std::string clock(double seconds) {
    const auto total = static_cast<std::int64_t>(seconds);
    const std::int64_t days = total / 86400;
    const std::int64_t hours = (total % 86400) / 3600;
    const std::int64_t minutes = (total % 3600) / 60;

    std::ostringstream os;
    os << days << "d " << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setw(2) << minutes;
    return os.str();
}

}  // namespace

namespace {

constexpr size_t kCsvColumns = 30;

// Writes exactly kCsvColumns fields. Counting separators by hand is what
// produced rows with the wrong number of columns; this cannot.
std::string csvRow(const std::vector<std::string>& fields) {
    std::string out;
    for (size_t i = 0; i < kCsvColumns; ++i) {
        if (i > 0) {
            out += ',';
        }
        if (i < fields.size()) {
            out += fields[i];
        }
    }
    out += '\n';
    return out;
}

std::string quotedField(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        out += (c == '"') ? '\'' : c;
    }
    out += '"';
    return out;
}

std::string num(double v, int precision) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

}  // namespace

EventLog::~EventLog() { close(); }

bool EventLog::open(const Config& cfg, bool force) {
    if (!force && !cfg.req<bool>("log.enabled")) {
        return false;
    }

    const std::string dir = cfg.req<std::string>("log.directory");
    const std::string format = cfg.req<std::string>("log.format");
    const std::string stem = cfg.req<std::string>("log.file_stem");
    if (stem.empty()) {
        throw ConfigError("log.file_stem cannot be empty");
    }

    if (format != "csv" && format != "text" && format != "both") {
        throw ConfigError("log.format must be \"csv\", \"text\" or \"both\"");
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    if (format == "csv" || format == "both") {
        csvPath_ = dir + "/" + stem + ".csv";
        csv_.open(csvPath_, std::ios::trunc);
        if (!csv_) {
            throw ConfigError("cannot open log file: " + csvPath_);
        }
        csv_ << "tick,day,time,event,"
                "actor,actor_kind,actor_side,row,col,actor_state,"
                "actor_hp,actor_hp_pct,actor_soldiers,"
                "actor_munitions,actor_fuel,actor_supplies,"
                "target,target_side,target_row,target_col,target_state,"
                "target_hp,target_hp_pct,target_soldiers,"
                "target_munitions,target_fuel,target_supplies,"
                "value1,value2,note\n";
    }

    if (format == "text" || format == "both") {
        textPath_ = dir + "/" + stem + ".log";
        text_.open(textPath_, std::ios::trunc);
        if (!text_) {
            throw ConfigError("cannot open log file: " + textPath_);
        }
    }

    const std::string audit = cfg.req<std::string>("log.audit");
    if (audit != "strict" && audit != "sitrep" && audit != "off") {
        throw ConfigError("log.audit must be \"strict\", \"sitrep\" or \"off\"");
    }
    auditStrict_ = (audit == "strict");

    lines_ = 0;
    anomalies_ = 0;
    lastBlue_ = -1;
    lastRed_ = -1;
    return enabled();
}

void EventLog::record(const Simulation& sim, LogEvent type, const Entity& actor,
                      const Entity* target, double value1, double value2) {
    if (!enabled()) {
        return;
    }
    ++lines_;

    const double seconds = sim.ticksToSeconds(sim.now());
    const double days = seconds / 86400.0;

    // Health and resources live on DynamicUnit rather than Entity, so that
    // static assets can share the same base class. Read them where they exist.
    const auto* actorUnit = dynamic_cast<const DynamicUnit*>(&actor);
    const auto* targetUnit = dynamic_cast<const DynamicUnit*>(target);

    const auto* actorInf = dynamic_cast<const InfantryBattalion*>(&actor);
    const auto* targetInf = dynamic_cast<const InfantryBattalion*>(target);

    if (csv_.is_open()) {
        csv_ << sim.now() << ',' << std::fixed << std::setprecision(4) << days
             << ',' << clock(seconds) << ',' << logEventName(type) << ','
             << actor.id() << ',' << actor.kind() << ',' << sideName(actor.side())
             << ',' << actor.loc().row << ',' << actor.loc().col << ',';
        csv_ << (actorInf ? InfantryBattalion::stateName(actorInf->state()) : "")
             << ',';
        writeUnitColumns(actorUnit);

        if (target != nullptr) {
            csv_ << target->id() << ',' << sideName(target->side()) << ','
                 << target->loc().row << ',' << target->loc().col << ',';
            csv_ << (targetInf ? InfantryBattalion::stateName(targetInf->state())
                               : "")
                 << ',';
        } else {
            csv_ << ",,,,,";
        }
        writeUnitColumns(targetUnit);

        csv_ << std::setprecision(3) << value1 << ',' << value2 << ",\n";
    }

    if (text_.is_open()) {
        text_ << '[' << clock(seconds) << "] " << sideName(actor.side()) << ' '
              << actor.kind() << " #" << actor.id() << ' ' << actor.loc().str();

        switch (type) {
            case LogEvent::Deployed:
                text_ << " deployed at full strength" << soldiersOf(actorUnit);
                break;
            case LogEvent::Moved:
                text_ << " moved (" << std::fixed << std::setprecision(2)
                      << value1 << " boxes, fuel " << std::setprecision(1)
                      << value2 << ")";
                break;
            case LogEvent::Shot:
                text_ << " fired on " << (target ? sideName(target->side()) : "?")
                      << " #" << (target ? target->id() : -1) << ' '
                      << (target ? target->loc().str() : "") << ": "
                      << std::setprecision(1) << value1 << " damage"
                      << casualtiesOf(value1, targetUnit ? targetUnit->hpMax() : 0.0)
                      << ", target at "
                      << std::setprecision(1) << value2;
                if (targetUnit != nullptr) {
                    text_ << '/' << targetUnit->hpMax();
                }
                text_ << soldiersOf(targetUnit);
                if (actorUnit != nullptr) {
                    text_ << "; shooter ammo "
                          << std::setprecision(0)
                          << actorUnit->amount(Resource::Munitions);
                }
                break;
            case LogEvent::Missed:
                text_ << " fired on " << (target ? sideName(target->side()) : "?")
                      << " #" << (target ? target->id() : -1) << ' '
                      << (target ? target->loc().str() : "")
                      << " and missed (hit probability " << std::fixed
                      << std::setprecision(1) << (100.0 * value1) << "%)";
                if (actorUnit != nullptr) {
                    text_ << "; shooter ammo " << std::setprecision(0)
                          << actorUnit->amount(Resource::Munitions);
                }
                break;
            case LogEvent::NoAmmo:
                text_ << " has no ammunition left and cannot fire";
                break;
            case LogEvent::Stranded:
                text_ << " is out of fuel and cannot move";
                break;
            case LogEvent::Destroyed:
                text_ << " was destroyed by "
                      << (target ? sideName(target->side()) : "?") << " #"
                      << (target ? target->id() : -1);
                text_ << " - battalion wiped out";
                break;
            case LogEvent::Pursuing:
                text_ << " reached its objective and is heading for the enemy at ("
                      << std::setprecision(0) << value1 << "," << value2 << ")";
                break;
            default:
                text_ << ' ' << logEventName(type);
                break;
        }
        text_ << '\n';
    }
}

void EventLog::writeUnitColumns(const DynamicUnit* u) {
    // hp, hp_pct, soldiers, munitions, fuel, supplies — six fields, always.
    if (u == nullptr) {
        csv_ << ",,,,,,";
        return;
    }
    csv_ << std::setprecision(2) << u->hp() << ','
         << std::setprecision(1) << (100.0 * u->capabilityFactor()) << ',';
    if (soldiersFull_ > 0.0) {
        csv_ << std::setprecision(0) << (soldiersFull_ * u->capabilityFactor());
    }
    csv_ << ',' << std::setprecision(1) << u->amount(Resource::Munitions) << ','
         << u->amount(Resource::Fuel) << ',' << u->amount(Resource::Supplies)
         << ',';
}

std::string EventLog::soldiersOf(const DynamicUnit* u) const {
    if (u == nullptr || soldiersFull_ <= 0.0) {
        return "";
    }
    std::ostringstream os;
    os << " (~" << std::fixed << std::setprecision(0)
       << (soldiersFull_ * u->capabilityFactor()) << " of "
       << soldiersFull_ << " men)";
    return os.str();
}

std::string EventLog::casualtiesOf(double damage, double hpMax) const {
    if (soldiersFull_ <= 0.0 || damage <= 0.0 || hpMax <= 0.0) {
        return "";
    }
    // Damage as a share of the health pool, which stands for the battalion at
    // full strength. The pool comes from the unit itself, so no figure is
    // written into this file. Presentation only.
    std::ostringstream os;
    os << " (~" << std::fixed << std::setprecision(0)
       << (damage / hpMax * soldiersFull_) << " casualties)";
    return os.str();
}

void EventLog::situation(const Simulation& sim) {
    if (!enabled()) {
        return;
    }
    ++lines_;

    struct Tally {
        int alive = 0;
        int engaged = 0;
        int stranded = 0;
        int dry = 0;  // out of ammunition
        double hp = 0.0;
        double hpMax = 0.0;
        double munitions = 0.0;
        double fuel = 0.0;
        double supplies = 0.0;
    };
    Tally blue;
    Tally red;

    auto& mutableSim = const_cast<Simulation&>(sim);
    for (std::int32_t id = 0; id < sim.entityCount(); ++id) {
        const Entity* e = mutableSim.entity(id);
        if (e == nullptr || !e->alive()) {
            continue;
        }
        const auto* u = dynamic_cast<const DynamicUnit*>(e);
        if (u == nullptr) {
            continue;
        }
        Tally& t = (e->side() == Side::Blue) ? blue : red;
        ++t.alive;
        t.hp += u->hp();
        t.hpMax += u->hpMax();
        t.munitions += u->amount(Resource::Munitions);
        t.fuel += u->amount(Resource::Fuel);
        t.supplies += u->amount(Resource::Supplies);
        if (u->amount(Resource::Munitions) <= 0.0) {
            ++t.dry;
        }
        if (const auto* inf = dynamic_cast<const InfantryBattalion*>(e)) {
            if (inf->state() == InfantryBattalion::State::Engage) {
                ++t.engaged;
            } else if (inf->state() == InfantryBattalion::State::Stranded) {
                ++t.stranded;
            }
        }
    }

    const double seconds = sim.ticksToSeconds(sim.now());

    const auto men = [this](const Tally& t) {
        return (soldiersFull_ > 0.0 && t.hpMax > 0.0)
                   ? soldiersFull_ * static_cast<double>(t.alive) * (t.hp / t.hpMax)
                   : 0.0;
    };

    if (text_.is_open()) {
        text_ << '[' << clock(seconds) << "] SITUATION  "
              << "blue " << blue.alive << " units";
        if (soldiersFull_ > 0.0) {
            text_ << " (~" << std::fixed << std::setprecision(0) << men(blue)
                  << " men)";
        }
        text_ << ", " << std::setprecision(0)
              << (blue.hpMax > 0.0 ? 100.0 * blue.hp / blue.hpMax : 0.0)
              << "% strength, " << blue.engaged << " engaged, " << blue.stranded
              << " stranded, " << blue.dry << " dry"
              << "  |  red " << red.alive << " units";
        if (soldiersFull_ > 0.0) {
            text_ << " (~" << men(red) << " men)";
        }
        text_ << ", " << (red.hpMax > 0.0 ? 100.0 * red.hp / red.hpMax : 0.0)
              << "% strength, " << red.engaged << " engaged, " << red.stranded
              << " stranded, " << red.dry << " dry\n";
    }

    if (csv_.is_open()) {
        const auto side = [&](const char* name, const Tally& t) {
            std::vector<std::string> f(kCsvColumns);
            f[0] = std::to_string(sim.now());
            f[1] = num(seconds / 86400.0, 4);
            f[2] = clock(seconds);
            f[3] = "situation";
            f[5] = name;                     // actor_kind
            f[6] = name;                     // actor_side
            f[9] = std::to_string(t.alive);  // actor_state holds the unit count
            f[10] = num(t.hp, 2);
            f[11] = num(t.hpMax > 0.0 ? 100.0 * t.hp / t.hpMax : 0.0, 1);
            f[12] = num(men(t), 0);
            f[13] = num(t.munitions, 1);
            f[14] = num(t.fuel, 1);
            f[15] = num(t.supplies, 1);
            f[27] = std::to_string(t.engaged);
            f[28] = std::to_string(t.stranded);
            f[29] = quotedField(std::to_string(t.dry) + " out of ammunition");
            csv_ << csvRow(f);
        };
        side("blue", blue);
        side("red", red);
    }
}

void EventLog::note(const Simulation& sim, LogEvent type,
                    const std::string& text) {
    if (!enabled()) {
        return;
    }
    ++lines_;

    const double seconds = sim.ticksToSeconds(sim.now());

    if (csv_.is_open()) {
        std::vector<std::string> f(kCsvColumns);
        f[0] = std::to_string(sim.now());
        f[1] = num(seconds / 86400.0, 4);
        f[2] = clock(seconds);
        f[3] = logEventName(type);
        f[29] = quotedField(text);
        csv_ << csvRow(f);
    }
    if (text_.is_open()) {
        text_ << '[' << clock(seconds) << "] " << text << '\n';
    }
}

std::uint64_t EventLog::audit(const Simulation& sim, const char* after) {
    if (!enabled()) {
        return anomalies_;
    }

    const World& w = sim.world();

    // 1. What the map says.
    std::int32_t cellBlue = 0;
    std::int32_t cellRed = 0;
    for (std::int32_t i = 0; i < w.cellCount(); ++i) {
        cellBlue += w.at(i).blueCount;
        cellRed += w.at(i).redCount;
    }

    // 2. What the occupancy lists say, and whether anyone is listed twice.
    std::int32_t listed = 0;
    std::int32_t duplicated = 0;
    std::vector<std::uint8_t> seen(static_cast<size_t>(sim.entityCount()), 0);
    for (std::int32_t i = 0; i < w.cellCount(); ++i) {
        w.forEachOccupant(w.locOf(i), [&](std::int32_t id) {
            ++listed;
            if (id >= 0 && id < static_cast<std::int32_t>(seen.size())) {
                if (seen[static_cast<size_t>(id)] != 0) {
                    ++duplicated;
                }
                seen[static_cast<size_t>(id)] = 1;
            }
        });
    }

    // 3. What the units themselves say.
    std::int32_t aliveBlue = 0;
    std::int32_t aliveRed = 0;
    auto& mutableSim = const_cast<Simulation&>(sim);
    for (std::int32_t id = 0; id < sim.entityCount(); ++id) {
        const Entity* e = mutableSim.entity(id);
        if (e == nullptr || !e->alive()) {
            continue;
        }
        if (e->side() == Side::Blue) {
            ++aliveBlue;
        } else if (e->side() == Side::Red) {
            ++aliveRed;
        }
    }

    std::string problem;
    const auto add = [&problem](const std::string& p) {
        if (!problem.empty()) {
            problem += "; ";
        }
        problem += p;
    };

    if (cellBlue != aliveBlue) {
        add("map shows " + std::to_string(cellBlue) + " blue but " +
            std::to_string(aliveBlue) + " are alive");
    }
    if (cellRed != aliveRed) {
        add("map shows " + std::to_string(cellRed) + " red but " +
            std::to_string(aliveRed) + " are alive");
    }
    if (w.sideCount(Side::Blue) != aliveBlue) {
        add("running blue total is " + std::to_string(w.sideCount(Side::Blue)) +
            ", alive " + std::to_string(aliveBlue));
    }
    if (w.sideCount(Side::Red) != aliveRed) {
        add("running red total is " + std::to_string(w.sideCount(Side::Red)) +
            ", alive " + std::to_string(aliveRed));
    }
    if (duplicated > 0) {
        add(std::to_string(duplicated) + " unit(s) listed in more than one box");
    }
    if (listed != aliveBlue + aliveRed) {
        add(std::to_string(listed) + " units listed on the map, " +
            std::to_string(aliveBlue + aliveRed) + " alive");
    }
    // Strength can only fall. A rise is the signature of double counting, which
    // is exactly the thing that is easy to see on screen and hard to pin down.
    if (lastBlue_ >= 0 && cellBlue > lastBlue_) {
        add("BLUE STRENGTH ROSE from " + std::to_string(lastBlue_) + " to " +
            std::to_string(cellBlue));
    }
    if (lastRed_ >= 0 && cellRed > lastRed_) {
        add("RED STRENGTH ROSE from " + std::to_string(lastRed_) + " to " +
            std::to_string(cellRed));
    }

    lastBlue_ = cellBlue;
    lastRed_ = cellRed;

    if (!problem.empty()) {
        ++anomalies_;
        note(sim, LogEvent::Anomaly,
             "ANOMALY after " + std::string(after) + ": " + problem);
    }

    return anomalies_;
}

void EventLog::close() {
    if (csv_.is_open()) {
        csv_.close();
    }
    if (text_.is_open()) {
        text_.close();
    }
}

}  // namespace wargame
