#include "entities/infantry/InfantryBattalion.h"

#include "pathfinding/AStar.h"
#include "config/Config.h"
#include "log/EventLog.h"
#include "engine/Simulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wargame {

const char* InfantryBattalion::stateName(State s) {
    switch (s) {
        case State::Advance: return "advance";
        case State::Approach: return "approach";
        case State::Engage: return "engage";
        case State::Stranded: return "stranded";
        case State::Dead: return "dead";
    }
    return "?";
}

InfantryBattalion::InfantryBattalion(const UnitProfile& profile,
                                     const PostCombatSurge& surge,
                                     const Tactics& tactics,
                                     const EngagementRules& rules, Side side,
                                     const GridPos& at, const GridPos& objective)
    : DynamicUnit(profile, surge, side, at),
      tactics_(tactics),
      rules_(rules),
      objective_(objective),
      stepTarget_(at) {}

Tick InfantryBattalion::travelTicks(double boxCost) const {
    const World& w = sim_->world();

    // Damage costs speed in the same proportion it costs everything else.
    // The floor keeps a nearly destroyed unit crawling rather than frozen.
    const double effectiveKmh =
        std::max(profile_->speedKmh * capabilityFactor(),
                 profile_->speedKmh * profile_->minSpeedFraction);

    const double hours = (w.boxKm() * boxCost) / effectiveKmh;
    return std::max<Tick>(1, sim_->secondsToTicks(hours * 3600.0));
}

void InfantryBattalion::scheduleWake(Tick at) {
    sim_->postEvent(Event(at, EventType::Wake, id_, id_));
}

std::int32_t InfantryBattalion::nearestEnemy(double rangeBoxes,
                                             double& outDistance) const {
    const World& w = sim_->world();

    std::int32_t best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    std::int32_t tied = 0;  // how many candidates share bestDistance so far

    // Walk the boxes in range and read their occupants, rather than testing
    // every unit in the simulation. With 2,000 units and a sensing range of a
    // box or two, this is the difference between a handful of reads and 2,000.
    const auto steps = static_cast<std::int32_t>(std::floor(rangeBoxes));
    const std::int32_t rLo = std::max(0, loc_.row - steps);
    const std::int32_t rHi = std::min(w.rows() - 1, loc_.row + steps);
    const std::int32_t cLo = std::max(0, loc_.col - steps);
    const std::int32_t cHi = std::min(w.cols() - 1, loc_.col + steps);

    Simulation* sim = sim_;
    const Side mySide = side_;
    const GridPos here = loc_;

    for (std::int32_t r = rLo; r <= rHi; ++r) {
        for (std::int32_t c = cLo; c <= cHi; ++c) {
            const GridPos at(r, c);
            const double d = w.octileDistance(here, at);
            if (d > rangeBoxes + 1e-9) {
                continue;
            }
            // Only boxes holding the other side are worth opening.
            const Cell& cell = w.at(at);
            const bool enemyHere = (mySide == Side::Blue) ? (cell.redCount > 0)
                                                          : (cell.blueCount > 0);
            if (!enemyHere) {
                continue;
            }

            w.forEachOccupant(at, [&](std::int32_t otherId) {
                Entity* e = sim->entity(otherId);
                if (e == nullptr || !e->alive() || e->side() == mySide) {
                    return;
                }
                if (d < bestDistance) {
                    bestDistance = d;
                    best = otherId;
                    tied = 1;
                } else if (d == bestDistance) {
                    if (sim->randomTieBreak()) {
                        // Among equals pick one at random, each equally likely
                        // (reservoir sampling), from the seeded RNG. Preferring
                        // the lowest id here favoured whichever army happened
                        // to be numbered first.
                        ++tied;
                        if (sim->rng().uniformInt(1, tied) == 1) {
                            best = otherId;
                        }
                    } else if (otherId < best) {
                        // creation_order mode: the lower id, so the choice does
                        // not depend on the order the list happens to be in.
                        best = otherId;
                    }
                }
            });
        }
    }

    outDistance = bestDistance;
    return best;
}

GridPos InfantryBattalion::bestStepToward(const GridPos& target) const {
    const World& w = sim_->world();

    GridPos best = loc_;
    double bestDistance = w.octileDistance(loc_, target);

    for (std::int32_t dr = -1; dr <= 1; ++dr) {
        for (std::int32_t dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) {
                continue;
            }
            const GridPos candidate(loc_.row + dr, loc_.col + dc);
            if (!w.inBounds(candidate) || !w.at(candidate).passableForLand()) {
                continue;
            }
            const double d = w.octileDistance(candidate, target);
            if (d < bestDistance) {
                bestDistance = d;
                best = candidate;
            }
        }
    }
    return best;
}

bool InfantryBattalion::stepTo(const GridPos& next, Tick now) {
    if (next == loc_) {
        return false;
    }
    const World& w = sim_->world();
    const double boxCost = w.stepCost(loc_, next);

    // Fuel is charged on departure: a unit that cannot pay does not set off.
    const double needed = profile_->fuelPerBox * boxCost;
    if (!has(Resource::Fuel, needed)) {
        consume(Resource::Fuel, needed);  // drains the remainder and flags it
        state_ = State::Stranded;
        if (EventLog* lg = sim_->log()) {
            lg->record(*sim_, LogEvent::Stranded, *this);
        }
        // No event is scheduled here: scheduling is the caller's job. Doing it
        // in both places gave a unit two pending wakes, each of which then
        // produced two more — the queue doubled on every decision until the
        // process ran out of memory.
        return false;
    }
    spendFuelForMove(boxCost);
    sim_->noteActivity();  // setting off counts as activity (stalemate detection)

    if (EventLog* lg = sim_->log()) {
        lg->record(*sim_, LogEvent::Moved, *this, nullptr, boxCost,
                   amount(Resource::Fuel));
    }

    stepTarget_ = next;
    Event arrive(now + travelTicks(boxCost), EventType::Arrive, id_, id_);
    arrive.addArg(w.index(next));
    sim_->postEvent(arrive);
    return true;
}

void InfantryBattalion::fireAt(std::int32_t targetId, Tick now) {
    Entity* e = sim_->entity(targetId);
    if (e == nullptr || !e->alive()) {
        return;
    }
    auto* target = static_cast<DynamicUnit*>(e);

    if (spendMunitionsForShot() <= 0.0) {
        if (EventLog* lg = sim_->log()) {
            lg->record(*sim_, LogEvent::NoAmmo, *this, target);
        }
        // Out of ammunition. The unit stays in contact but contributes
        // nothing — the "empty magazine" the supervisor wants the model to
        // show.
        return;
    }
    ++shotsFired;
    sim_->noteActivity();  // firing counts as activity (stalemate detection)

    // A shot lands with the weapon's hit probability, lowered by the electronic
    // warfare that covers the target. One draw per shot from the seeded RNG, hit
    // or miss, so the stream stays in step whatever the probability is.
    const double hitProbability = rules_.hitProbability(
        profile_->hitProbability, target->profile().ewCoverage);
    const bool hit = sim_->rng().chance(hitProbability);

    // Being shot at is combat whether or not the shot lands: both sides start (or
    // extend) the post-battle supply surge, and the target settles its clock.
    target->accrueTo(now);
    markCombat(now);
    target->markCombat(now);

    if (!hit) {
        if (EventLog* lg = sim_->log()) {
            lg->record(*sim_, LogEvent::Missed, *this, target, hitProbability);
        }
        return;
    }
    ++shotsHit;

    // Damage is the weapon's fixed attack, scaled by how much of the shooter is
    // left (the linear loss of capability).
    const double damage = profile_->attack * capabilityFactor();
    const bool destroyed = target->applyDamage(damage);

    if (EventLog* lg = sim_->log()) {
        lg->record(*sim_, LogEvent::Shot, *this, target, damage, target->hp());
        if (destroyed) {
            lg->record(*sim_, LogEvent::Destroyed, *target, this);
        }
    }

    if (destroyed) {
        ++killsScored;
        sim_->world().removeOccupant(target->loc(), targetId, target->side());

        Event died(now, EventType::Destroyed, id_, targetId);
        died.addArg(id_);
        sim_->postEvent(died);

        // The battle is decided once a side has nothing left on the map.
        // Carrying on to the end of the mission window would only simulate an
        // empty battlefield.
        if (sim_->world().sideCount(target->side()) == 0) {
            sim_->requestStop(std::string(sideName(target->side())) +
                              " eliminated");
        }
    }
}

void InfantryBattalion::evaluate(Tick now) {
    if (!alive_) {
        state_ = State::Dead;
        return;
    }

    double distance = 0.0;

    // In reach? Fire.
    const std::int32_t inReach =
        nearestEnemy(static_cast<double>(profile_->attackRangeBoxes), distance);
    if (inReach >= 0) {
        state_ = State::Engage;
        fireAt(inReach, now);
        scheduleWake(now + sim_->secondsToTicks(tactics_.engagementSeconds));
        return;
    }

    // In sight? Close on it.
    const std::int32_t seen =
        nearestEnemy(static_cast<double>(profile_->sensingRangeBoxes), distance);
    if (seen >= 0) {
        Entity* e = sim_->entity(seen);
        if (e != nullptr && e->alive()) {
            state_ = State::Approach;
            if (stepTo(bestStepToward(e->loc()), now)) {
                return;
            }
            scheduleWake(now + sim_->secondsToTicks(tactics_.decisionSeconds));
            return;
        }
    }

    // Nothing in sight: carry on toward the objective.
    state_ = State::Advance;
    if (pathIndex_ < path_.size()) {
        if (stepTo(path_[pathIndex_], now)) {
            return;
        }
        scheduleWake(now + sim_->secondsToTicks(tactics_.decisionSeconds));
        return;
    }

    // Objective reached with nothing in sight. Rather than stand there for the
    // rest of the campaign, head for where the enemy actually is. See the note
    // on Tactics::pursue: this is a pilot assumption about strategic
    // intelligence, not a modelled sensor.
    if (tactics_.pursue) {
        const Side enemy = (side_ == Side::Blue) ? Side::Red : Side::Blue;
        GridPos centre;
        if (sim_->world().sideCentre(enemy, centre) && centre != loc_) {
            if (objective_ != centre) {
                if (EventLog* lg = sim_->log()) {
                    lg->record(*sim_, LogEvent::Pursuing, *this, nullptr,
                               centre.row, centre.col);
                }
            }
            objective_ = centre;
            sim_->pathfinder().findPath(sim_->world(), loc_, objective_, path_);
            pathIndex_ = 0;
            if (pathIndex_ < path_.size() && stepTo(path_[pathIndex_], now)) {
                return;
            }
        }
    }

    // Nowhere to go: hold, and look again later.
    scheduleWake(now + sim_->secondsToTicks(tactics_.decisionSeconds));
}

bool InfantryBattalion::onEvent(const Event& e) {
    accrueTo(e.time);

    switch (e.type) {
        case EventType::SimStart: {
            lastAccrual_ = e.time;

            if (!registered_) {
                sim_->world().addOccupant(loc_, id_, side_);
                registered_ = true;
                if (EventLog* lg = sim_->log()) {
                    lg->record(*sim_, LogEvent::Deployed, *this);
                }
            }

            // The long route to the objective, computed once. Local approach
            // to an enemy is handled step by step instead, which on open
            // ground needs no search at all.
            sim_->pathfinder().findPath(sim_->world(), loc_, objective_, path_);
            pathIndex_ = 0;

            // Spread the first decision over one interval so 2,000 units do
            // not all think on the same tick.
            const Tick spread = sim_->secondsToTicks(
                sim_->rng().uniform(0.0, tactics_.decisionSeconds));
            scheduleWake(e.time + spread);
            return true;
        }

        case EventType::Wake:
            evaluate(e.time);
            return true;

        case EventType::Arrive: {
            sim_->world().moveOccupant(loc_, stepTarget_, id_, side_);
            loc_ = stepTarget_;
            ++boxesMoved;

            if (pathIndex_ < path_.size() && path_[pathIndex_] == loc_) {
                ++pathIndex_;
            }
            evaluate(e.time);
            return true;
        }

        case EventType::Destroyed:
            // Addressed to this unit: it has just been killed.
            state_ = State::Dead;
            return true;

        case EventType::SimStop:
            return true;

        default:
            return false;
    }
}

}  // namespace wargame
