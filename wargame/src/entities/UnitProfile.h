// What one type of unit is like.
//
// Shared by every unit of that type, so 2,000 units do not each carry a copy of
// the same numbers. Every field is read from the config; nothing is defaulted
// to a real value.

#pragma once

#include "entities/Resource.h"

#include <array>
#include <cstdint>
#include <string>

namespace wargame {

class Config;

struct UnitProfile {
    std::string name;

    double hp = 0.0;
    double attack = 0.0;
    double hitProbability = 0.0;  // chance that one shot lands, before any EW
    double ewCoverage = 0.0;      // 0..1: share of the EW reduction that applies to
                                  // shots at this unit (0 = none)
    std::int32_t attackRangeBoxes = 0;
    std::int32_t sensingRangeBoxes = 0;
    double speedKmh = 0.0;
    double minSpeedFraction = 0.0;  // floor on speed as a fraction of full speed

    std::array<double, kResourceCount> capacity{};

    // Consumed per day regardless of what the unit does. Munitions and fuel are
    // normally zero here: a parked aircraft burns no fuel, while an infantry
    // battalion still eats.
    std::array<double, kResourceCount> basePerDay{};

    double fuelPerBox = 0.0;             // charged on departure toward a new box
    double munitionsPerEngagement = 0.0;  // charged when the unit opens fire

    // Loads a profile from a config subtree, e.g. "units.infantry_battalion".
    static UnitProfile load(const Config& cfg, const std::string& path);
};

}  // namespace wargame
