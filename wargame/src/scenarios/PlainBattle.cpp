#include "scenarios/PlainBattle.h"

#include "config/Config.h"
#include "engine/Simulation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

namespace wargame {

namespace {

// Lays n units out in a block as square as the count allows.
struct Block {
    std::int32_t cols = 0;
    std::int32_t rows = 0;
};

Block blockFor(std::int32_t n) {
    Block b;
    b.cols = std::max<std::int32_t>(
        1, static_cast<std::int32_t>(std::ceil(std::sqrt(
               static_cast<double>(n)))));
    b.rows = (n + b.cols - 1) / b.cols;
    return b;
}

}  // namespace

void PlainBattle::build(Simulation& sim, const Config& cfg) {
    profile_ = UnitProfile::load(cfg, "units.infantry_battalion");
    surge_ = PostCombatSurge::load(cfg);
    tactics_ = Tactics::load(cfg, "units.infantry_battalion");
    rules_ = EngagementRules::load(cfg);

    perSide_ = cfg.req<std::int32_t>("scale.pilot_units_per_side");
    const auto margin = cfg.req<std::int32_t>("scenario.margin_boxes");
    const auto spacing = cfg.req<std::int32_t>("scenario.spacing_boxes");
    if (spacing < 1) {
        throw ConfigError("scenario.spacing_boxes must be at least 1");
    }

    const World& w = sim.world();
    const Block block = blockFor(perSide_);

    // Size of one side's formation on the grid, in boxes: units sit `spacing`
    // boxes apart, so a block of n columns spans (n - 1) * spacing + 1 boxes.
    const std::int32_t width = (block.cols - 1) * spacing + 1;
    const std::int32_t height = (block.rows - 1) * spacing + 1;

    if (width * 2 + margin * 2 >= w.cols() || height > w.rows()) {
        throw ConfigError(
            "scenario: " + std::to_string(perSide_) +
            " units a side, " + std::to_string(spacing) +
            " box(es) apart, do not fit on a " + std::to_string(w.rows()) + "x" +
            std::to_string(w.cols()) + " grid with a margin of " +
            std::to_string(margin) + " boxes");
    }

    const std::int32_t firstRow = (w.rows() - height) / 2;
    const std::int32_t blueCol = margin;
    const std::int32_t redCol = w.cols() - 1 - margin - (width - 1);

    blue_.clear();
    red_.clear();
    blue_.reserve(static_cast<size_t>(perSide_));
    red_.reserve(static_cast<size_t>(perSide_));

    for (std::int32_t i = 0; i < perSide_; ++i) {
        const std::int32_t r = firstRow + (i / block.cols) * spacing;
        const std::int32_t c = (i % block.cols) * spacing;

        const GridPos bluePos(r, blueCol + c);
        const GridPos redPos(r, redCol + c);

        // Each battalion marches on the box its opposite number starts from,
        // rather than on one shared point. Giving every unit the same
        // destination funnels a whole side into a single box and lets the two
        // forces walk straight past each other; mirrored objectives make them
        // advance as a line and meet in the middle, which is what a plain
        // battle looks like.
        auto b = std::make_unique<InfantryBattalion>(
            profile_, surge_, tactics_, rules_, Side::Blue, bluePos, redPos);
        blue_.push_back(b.get());
        sim.addEntity(std::move(b));

        auto d = std::make_unique<InfantryBattalion>(
            profile_, surge_, tactics_, rules_, Side::Red, redPos, bluePos);
        red_.push_back(d.get());
        sim.addEntity(std::move(d));
    }

    sim.world().reserveEntities(sim.entityCount());
}

SideReport PlainBattle::report(Side s) const {
    const std::vector<InfantryBattalion*>& units = sideUnits(s);

    SideReport r;
    r.total = static_cast<std::int32_t>(units.size());

    double hpSum = 0.0;
    double supplySum = 0.0;

    for (const InfantryBattalion* u : units) {
        r.shots += u->shotsFired;
        r.hits += u->shotsHit;
        r.kills += u->killsScored;
        r.boxesMoved += u->boxesMoved;
        if (u->alive()) {
            ++r.alive;
            hpSum += u->capabilityFactor();
        }
        if (u->state() == InfantryBattalion::State::Stranded) {
            ++r.stranded;
        }
        if (u->amount(Resource::Munitions) <= 0.0) {
            ++r.outOfAmmo;
        }
        supplySum += u->fraction(Resource::Supplies);
    }

    r.meanHpFraction = (r.alive > 0) ? hpSum / r.alive : 0.0;
    r.meanSuppliesFraction =
        (r.total > 0) ? supplySum / static_cast<double>(r.total) : 0.0;
    return r;
}

std::string PlainBattle::summary(const Simulation& sim) const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);

    const SideReport b = report(Side::Blue);
    const SideReport r = report(Side::Red);

    os << "  " << std::setw(6) << "side" << std::setw(10) << "alive"
       << std::setw(10) << "shots" << std::setw(9) << "hits"
       << std::setw(9) << "kills" << std::setw(10)
       << "moved" << std::setw(11) << "stranded" << std::setw(11) << "no ammo"
       << std::setw(10) << "mean hp" << std::setw(12) << "supplies" << "\n";

    const auto line = [&os](const char* name, const SideReport& s) {
        os << "  " << std::setw(6) << name << std::setw(7) << s.alive << "/"
           << std::setw(2) << s.total << std::setw(10) << s.shots
           << std::setw(9) << s.hits
           << std::setw(9) << s.kills << std::setw(10) << s.boxesMoved
           << std::setw(11) << s.stranded << std::setw(11) << s.outOfAmmo
           << std::setw(9) << (100.0 * s.meanHpFraction) << "%"
           << std::setw(11) << (100.0 * s.meanSuppliesFraction) << "%\n";
    };

    line("blue", b);
    line("red", r);

    // One glyph on the map is one box, however many units stand in it, so the
    // worst stack is worth stating outright.
    std::int32_t worstStack = 0;
    const World& w = sim.world();
    for (std::int32_t i = 0; i < w.cellCount(); ++i) {
        const Cell& c = w.at(i);
        worstStack = std::max<std::int32_t>(worstStack, c.blueCount + c.redCount);
    }

    os << "  simulated " << std::setprecision(1)
       << (sim.ticksToSeconds(sim.stats().endTick) / 86400.0)
       << " days, most units in one box: " << worstStack << "\n";
    return os.str();
}

std::string PlainBattle::map(const Simulation& sim, std::int32_t maxWidth) const {
    return sim.world().ascii(maxWidth);
}

}  // namespace wargame
