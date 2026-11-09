// An infantry battalion — the only fighting unit in the pilot.
//
// Base unit of account, as settled in the 4/10 meeting: a battalion, with a
// health pool of 45, light-weapon attack 5, and a reach of one box. It is the
// only arm that can take and hold ground, though holding is not modelled yet.
//
// The behaviour is a small state machine. The supervisor's rover uses the State
// pattern with one class per state; here the states are an enum with explicit
// transitions, because for the pilot he asked for only "one, two, three
// classes" rather than an investment in many. The virtual dispatch he wanted
// measured still happens on every event through Entity::onEvent.

#pragma once

#include "entities/DynamicUnit.h"
#include "entities/EngagementRules.h"
#include "entities/infantry/Tactics.h"

#include <vector>

namespace wargame {

class Config;

class InfantryBattalion : public DynamicUnit {
  public:
    enum class State : std::uint8_t {
        Advance,   // moving toward the objective
        Approach,  // an enemy is in sight; closing on it
        Engage,    // an enemy is in reach; firing
        Stranded,  // out of fuel, cannot move
        Dead,
    };

    InfantryBattalion(const UnitProfile& profile, const PostCombatSurge& surge,
                      const Tactics& tactics, const EngagementRules& rules,
                      Side side, const GridPos& at, const GridPos& objective);

    State state() const { return state_; }
    static const char* stateName(State s);

    bool onEvent(const Event& e) override;

    // Counters, read after a run.
    std::int64_t shotsFired = 0;
    std::int64_t shotsHit = 0;  // of shotsFired, how many landed
    std::int64_t boxesMoved = 0;
    std::int64_t killsScored = 0;

  private:
    void evaluate(Tick now);

    // Nearest living enemy within `rangeBoxes`, or -1. Scans the boxes in
    // range through the world's occupancy index rather than every entity.
    std::int32_t nearestEnemy(double rangeBoxes, double& outDistance) const;

    // Starts a step into an adjacent box: charges the fuel and schedules the
    // arrival for when the unit would really get there. Returns false if the
    // unit has no fuel left.
    bool stepTo(const GridPos& next, Tick now);

    // The adjacent box that most reduces the distance to `target`. On open
    // ground this is all the local approach needs; A* is for the long haul to
    // the objective.
    GridPos bestStepToward(const GridPos& target) const;

    void fireAt(std::int32_t targetId, Tick now);

    // Travel time for one step, in ticks. Speed falls with damage, so a
    // battered battalion really does move more slowly.
    Tick travelTicks(double boxCost) const;

    void scheduleWake(Tick at);

    Tactics tactics_;
    EngagementRules rules_;
    State state_ = State::Advance;

    GridPos objective_;
    std::vector<GridPos> path_;   // route to the objective, from A*
    size_t pathIndex_ = 0;

    GridPos stepTarget_;          // the box an in-flight Arrive event refers to
    bool registered_ = false;  // placed in the occupancy index
};

}  // namespace wargame
