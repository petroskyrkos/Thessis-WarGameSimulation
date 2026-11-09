// A unit that moves, fights and consumes resources.
//
// PHASE 1: classical (monolithic) forces. A unit carries its own full set of
// capabilities. Nothing here splits capabilities across entities.
//
// Consumption is charged lazily. This is a discrete-event simulation, so the
// engine never walks every unit on every tick; instead a unit remembers when it
// was last charged and settles the whole elapsed interval the next time it is
// touched. That is the supervisor's _peTime pattern, generalised.

#pragma once

#include "engine/Event.h"
#include "entities/Entity.h"
#include "entities/PostCombatSurge.h"
#include "entities/Resource.h"
#include "entities/UnitProfile.h"

#include <array>
#include <cstdint>

namespace wargame {

class DynamicUnit : public Entity {
  public:
    DynamicUnit(const UnitProfile& profile, const PostCombatSurge& surge,
                Side side, const GridPos& at);

    const char* kind() const override { return profile_->name.c_str(); }
    const UnitProfile& profile() const { return *profile_; }

    // ---- condition ----

    double hp() const { return hp_; }
    double hpMax() const { return profile_->hp; }

    // Damage translates straight into lost capability: a unit at 50% damage
    // fights at 50% strength, moves at 50% and so on. The supervisor's linear
    // model, applied to everything.
    double capabilityFactor() const {
        return (profile_->hp > 0.0) ? (hp_ / profile_->hp) : 0.0;
    }

    // Returns true if this hit destroyed the unit.
    bool applyDamage(double amount);

    // ---- resources ----

    double amount(Resource r) const { return pool_[resourceIndex(r)]; }
    double capacity(Resource r) const { return profile_->capacity[resourceIndex(r)]; }
    double fraction(Resource r) const {
        const double cap = capacity(r);
        return (cap > 0.0) ? (pool_[resourceIndex(r)] / cap) : 0.0;
    }
    bool has(Resource r, double need) const { return pool_[resourceIndex(r)] >= need; }

    // Fills every pool to capacity. Used when a unit is created or resupplied.
    void fillAll();

    // Takes what it can and returns how much was actually taken. A unit that
    // runs dry is not an error — it is the whole point of the logistics model.
    double consume(Resource r, double want);
    double add(Resource r, double want);

    // True when a pool has run empty since the last time it was refilled.
    bool ranOut(Resource r) const { return ranOut_[resourceIndex(r)]; }

    // ---- the lazy clock ----

    // Charges the time-driven consumption for everything between the last
    // settlement and `now`, then moves the marker. Safe to call repeatedly; a
    // second call with the same tick charges nothing.
    void accrueTo(Tick now);

    // Records that the unit was in combat at `now`, which starts or extends the
    // supply surge. Cumulative: a battle while a window is still open adds a
    // further `days` on top of what was left, so a window with 4 days to run
    // becomes 11 days from this moment.
    void markCombat(Tick now);

    Tick surgeUntil() const { return surgeUntil_; }

    // ---- effort-driven consumption ----

    // Charges fuel for crossing into a new box. `boxCost` is the octile cost of
    // the step: 1 for a straight move, sqrt(2) for a diagonal one, so a
    // diagonal really does burn more.
    double spendFuelForMove(double boxCost);

    // Charges munitions for opening fire.
    double spendMunitionsForShot();

    void setLoc(const GridPos& l) { loc_ = l; }

    bool onEvent(const Event& e) override;

  protected:
    const UnitProfile* profile_;  // not owned; shared by every unit of the type
    PostCombatSurge surge_;

    double hp_ = 0.0;
    std::array<double, kResourceCount> pool_{};
    std::array<bool, kResourceCount> ranOut_{};

    Tick lastAccrual_ = 0;
    Tick surgeUntil_ = 0;  // ticks; the supply surge is active before this
};

}  // namespace wargame
