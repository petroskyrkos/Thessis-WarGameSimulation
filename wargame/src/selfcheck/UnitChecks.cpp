// Checks of units and resources: the lazy clock, the post-combat surge, damage
// and the three pools.

#include "config/Config.h"
#include "engine/Simulation.h"
#include "entities/DynamicUnit.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"

#include <cmath>
#include <memory>

namespace wargame::selfcheck {

namespace {

// Builds one unit inside a fresh engine, so each check starts clean.
struct UnitFixture {
    wargame::Simulation sim;
    wargame::DynamicUnit* unit = nullptr;

    UnitFixture(const wargame::Config& cfg, const wargame::UnitProfile& profile,
                const wargame::PostCombatSurge& surge) {
        sim.init(cfg);
        auto u = std::make_unique<wargame::DynamicUnit>(profile, surge,
                                                   wargame::Side::Blue, wargame::GridPos(1, 1));
        unit = u.get();
        sim.addEntity(std::move(u));
    }

    wargame::Tick day(double d) const { return sim.daysToTicks(d); }
};

}  // namespace

void runUnitChecks(const wargame::Config& cfg) {
    const wargame::UnitProfile profile =
        wargame::UnitProfile::load(cfg, "units.infantry_battalion");
    const wargame::PostCombatSurge surge = wargame::PostCombatSurge::load(cfg);

    const double basePerDay =
        profile.basePerDay[wargame::resourceIndex(wargame::Resource::Supplies)];
    const double cap = profile.capacity[wargame::resourceIndex(wargame::Resource::Supplies)];
    const double tol = 0.01;  // far above the sub-tick rounding drift

    const auto consumedSupplies = [&](const wargame::DynamicUnit& u) {
        return cap - u.amount(wargame::Resource::Supplies);
    };

    // A unit that never fights consumes exactly the base rate.
    {
        UnitFixture f(cfg, profile, surge);
        f.unit->accrueTo(f.day(10));
        report("10 quiet days consume 10 days of base supplies",
               std::abs(consumedSupplies(*f.unit) - 10.0 * basePerDay) < tol);
    }

    // Settling the same span in many small steps must give the same answer as
    // settling it in one. If it does not, the lazy clock is losing or
    // double-charging time.
    {
        UnitFixture a(cfg, profile, surge);
        a.unit->accrueTo(a.day(10));

        UnitFixture b(cfg, profile, surge);
        for (int d = 1; d <= 10; ++d) {
            b.unit->accrueTo(b.day(d));
        }
        report("one big settlement equals ten small ones",
               std::abs(consumedSupplies(*a.unit) - consumedSupplies(*b.unit)) <
                   tol);
    }

    // Settling again at the same tick must charge nothing.
    {
        UnitFixture f(cfg, profile, surge);
        f.unit->accrueTo(f.day(5));
        const double once = consumedSupplies(*f.unit);
        f.unit->accrueTo(f.day(5));
        report("settling twice at the same tick charges once",
               std::abs(consumedSupplies(*f.unit) - once) < 1e-12);
    }

    // After a battle, supplies run at the surge multiplier for one week.
    {
        UnitFixture f(cfg, profile, surge);
        f.unit->markCombat(0);
        f.unit->accrueTo(f.day(7));
        report("the week after a battle consumes at the surge multiplier",
               std::abs(consumedSupplies(*f.unit) -
                        surge.multiplier * surge.days * basePerDay) < tol);
    }

    // Sleeping through the end of the window must charge the surge for the part
    // inside it and the base rate for the rest — the interval has to be split.
    {
        UnitFixture f(cfg, profile, surge);
        f.unit->markCombat(0);
        f.unit->accrueTo(f.day(10));
        const double expected =
            (surge.multiplier * surge.days + (10.0 - surge.days)) * basePerDay;
        report("an interval spanning the end of the window is split",
               std::abs(consumedSupplies(*f.unit) - expected) < tol);
    }

    // The supervisor's own example: a battle on day 3, with four days left of
    // the first window, means eleven more days of surge — to day 14, not day 10.
    {
        UnitFixture f(cfg, profile, surge);
        f.unit->markCombat(0);
        f.unit->accrueTo(f.day(3));
        f.unit->markCombat(f.day(3));

        report("a second battle extends the window cumulatively",
               f.unit->surgeUntil() == f.day(7) + f.day(7));

        f.unit->accrueTo(f.day(14));
        report("the extended window surges for its whole length",
               std::abs(consumedSupplies(*f.unit) -
                        surge.multiplier * 14.0 * basePerDay) < tol);

        f.unit->accrueTo(f.day(20));
        report("consumption drops back to base once the window closes",
               std::abs(consumedSupplies(*f.unit) -
                        (surge.multiplier * 14.0 + 6.0) * basePerDay) < tol);
    }

    // Damage maps straight onto capability.
    {
        UnitFixture f(cfg, profile, surge);
        f.unit->applyDamage(profile.hp * 0.5);
        report("50% damage leaves 50% capability",
               std::abs(f.unit->capabilityFactor() - 0.5) < 1e-9);

        const bool killed = f.unit->applyDamage(profile.hp);
        report("damage beyond the health pool destroys the unit",
               killed && !f.unit->alive() && f.unit->capabilityFactor() == 0.0);
    }

    // A pool that runs dry clamps at zero and says so, rather than going
    // negative. Running out of supply is a modelled outcome, not a fault.
    {
        UnitFixture f(cfg, profile, surge);
        const double taken = f.unit->consume(wargame::Resource::Fuel, cap * 10.0);
        report("consuming more than is held takes only what is there",
               std::abs(taken - f.unit->capacity(wargame::Resource::Fuel)) < 1e-9);
        report("an empty pool clamps at zero and is flagged",
               f.unit->amount(wargame::Resource::Fuel) == 0.0 &&
                   f.unit->ranOut(wargame::Resource::Fuel));

        f.unit->add(wargame::Resource::Fuel, 1e9);
        report("resupply refills to capacity and no further",
               std::abs(f.unit->amount(wargame::Resource::Fuel) -
                        f.unit->capacity(wargame::Resource::Fuel)) < 1e-9);
    }

    // A diagonal step burns sqrt(2) times the fuel of a straight one, because
    // it really does cover that much more ground.
    {
        UnitFixture f(cfg, profile, surge);
        const double straight = f.unit->spendFuelForMove(1.0);
        const double diagonal =
            f.unit->spendFuelForMove(f.sim.world().diagonalCost());
        report("a diagonal move burns sqrt(2) times the fuel",
               straight > 0.0 &&
                   std::abs(diagonal / straight - f.sim.world().diagonalCost()) <
                       1e-9);
    }
}

}  // namespace wargame::selfcheck
