#include "entities/DynamicUnit.h"

#include "engine/Simulation.h"

#include <algorithm>

namespace wargame {

DynamicUnit::DynamicUnit(const UnitProfile& profile, const PostCombatSurge& surge,
                         Side side, const GridPos& at)
    : profile_(&profile), surge_(surge), hp_(profile.hp) {
    side_ = side;
    loc_ = at;
    fillAll();
}

void DynamicUnit::fillAll() {
    for (size_t i = 0; i < kResourceCount; ++i) {
        pool_[i] = profile_->capacity[i];
        ranOut_[i] = false;
    }
}

bool DynamicUnit::applyDamage(double amount) {
    if (amount <= 0.0 || !alive_) {
        return false;
    }
    hp_ -= amount;
    if (hp_ <= 0.0) {
        hp_ = 0.0;
        alive_ = false;
        return true;
    }
    return false;
}

double DynamicUnit::consume(Resource r, double want) {
    if (want <= 0.0) {
        return 0.0;
    }
    const size_t i = resourceIndex(r);
    const double taken = std::min(want, pool_[i]);
    pool_[i] -= taken;
    if (pool_[i] <= 0.0) {
        pool_[i] = 0.0;
        ranOut_[i] = true;
    }
    return taken;
}

double DynamicUnit::add(Resource r, double want) {
    if (want <= 0.0) {
        return 0.0;
    }
    const size_t i = resourceIndex(r);
    const double room = profile_->capacity[i] - pool_[i];
    const double added = std::min(want, std::max(0.0, room));
    pool_[i] += added;
    if (pool_[i] > 0.0) {
        ranOut_[i] = false;
    }
    return added;
}

void DynamicUnit::markCombat(Tick now) {
    if (sim_ == nullptr || surge_.days <= 0.0) {
        return;
    }
    const Tick window = sim_->daysToTicks(surge_.days);

    if (!surge_.cumulative || surgeUntil_ <= now) {
        // No window open, or windows do not stack: start a fresh one.
        surgeUntil_ = now + window;
        return;
    }

    // A window is still running. The supervisor's rule is additive: what is
    // left plus a further week. With 4 days left, a battle now means 11 days
    // from now, not 7.
    surgeUntil_ += window;
}

void DynamicUnit::accrueTo(Tick now) {
    if (sim_ == nullptr || now <= lastAccrual_) {
        return;
    }

    const Tick from = lastAccrual_;
    const Tick dt = now - from;
    const double secondsPerTick = sim_->tickSeconds();

    // Split the interval where the surge window ends. Charging the whole
    // interval at one multiplier would be wrong whenever a unit sleeps through
    // the end of its recovery week.
    Tick surgedTicks = 0;
    if (surgeUntil_ > from) {
        surgedTicks = std::min(dt, surgeUntil_ - from);
    }
    const Tick plainTicks = dt - surgedTicks;

    for (size_t i = 0; i < kResourceCount; ++i) {
        const double perDay = profile_->basePerDay[i];
        if (perDay <= 0.0) {
            continue;  // munitions and fuel have no time-driven component
        }
        const double perSecond = perDay / 86400.0;

        // Only supplies surge: a battle makes a unit need more food and
        // medicine, not more fuel while sitting still.
        const double multiplier =
            (static_cast<Resource>(i) == Resource::Supplies) ? surge_.multiplier
                                                             : 1.0;

        const double seconds =
            (static_cast<double>(plainTicks) +
             static_cast<double>(surgedTicks) * multiplier) *
            secondsPerTick;

        consume(static_cast<Resource>(i), perSecond * seconds);
    }

    lastAccrual_ = now;
}

double DynamicUnit::spendFuelForMove(double boxCost) {
    return consume(Resource::Fuel, profile_->fuelPerBox * boxCost);
}

double DynamicUnit::spendMunitionsForShot() {
    return consume(Resource::Munitions, profile_->munitionsPerEngagement);
}

bool DynamicUnit::onEvent(const Event& e) {
    // Any event is a chance to settle the clock: the unit is being touched, so
    // the time since it was last charged is now known.
    accrueTo(e.time);

    switch (e.type) {
        case EventType::SimStart:
            lastAccrual_ = e.time;
            return true;
        case EventType::SimStop:
            return true;
        default:
            return false;  // concrete unit types handle the rest
    }
}

}  // namespace wargame
