// The pilot scenario: a plain-field battle.
//
// From the 17/10 meeting: completely flat terrain, infantry only, about a
// hundred battalions a side. Naval units were ruled out for this test because
// there are only a few dozen of them and the point is to load the engine.
//
// This object owns the unit profile the battalions share, so it must outlive
// the run.

#pragma once

#include "entities/infantry/InfantryBattalion.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wargame {

class Config;
class Simulation;

struct SideReport {
    std::int32_t total = 0;
    std::int32_t alive = 0;
    std::int64_t shots = 0;
    std::int64_t hits = 0;  // of the shots, how many landed
    std::int64_t kills = 0;
    std::int64_t boxesMoved = 0;
    std::int32_t stranded = 0;      // out of fuel
    std::int32_t outOfAmmo = 0;
    double meanHpFraction = 0.0;
    double meanSuppliesFraction = 0.0;
};

class PlainBattle {
  public:
    // Places both sides and registers them with the engine. Call before run().
    void build(Simulation& sim, const Config& cfg);

    SideReport report(Side s) const;
    std::string summary(const Simulation& sim) const;

    // The grid with the units drawn on it.
    std::string map(const Simulation& sim, std::int32_t maxWidth) const;

    std::int32_t perSide() const { return perSide_; }

    // The battalions of one side, in creation order. For the state dump.
    const std::vector<InfantryBattalion*>& sideUnits(Side s) const {
        return (s == Side::Blue) ? blue_ : red_;
    }

  private:

    UnitProfile profile_;
    PostCombatSurge surge_;
    Tactics tactics_;
    EngagementRules rules_;

    std::int32_t perSide_ = 0;
    std::vector<InfantryBattalion*> blue_;  // owned by the Simulation
    std::vector<InfantryBattalion*> red_;
};

}  // namespace wargame
