// The rules that turn a shot into a hit or a miss.
//
// From the 10/10 meeting: firing does not guarantee a hit. A guided munition
// lands about 90-95% of the time, and electronic warfare (EW) lowers that by
// roughly 30-40%. The model keeps it as plain as the supervisor asked: one hit
// probability per weapon (in the unit profile) and one EW reduction (here).
//
// The supervisor's wording was "I subtract it", while PILOT.md says "x EW
// modifier". Both are supported and chosen in the config (damage.ew_mode), so
// the answer can be changed without touching the code once it is settled.

#pragma once

#include <cstdint>

namespace wargame {

class Config;

enum class EwMode : std::uint8_t {
    Multiplicative,  // p = base * (1 - reduction): loses a share of what it has
    Subtractive,     // p = base - reduction: loses percentage points, floor at 0
};

struct EngagementRules {
    double ewReduction = 0.0;                // damage.ew_reduction, in [0, 1]
    EwMode ewMode = EwMode::Multiplicative;  // damage.ew_mode

    // Probability that a shot lands. `base` is the weapon's hit probability and
    // `ewCoverage` (0..1) says how much of the EW reduction applies to the
    // target: 0 means no EW at all, 1 means the full reduction. The result is
    // always a valid probability.
    double hitProbability(double base, double ewCoverage) const;

    static EngagementRules load(const Config& cfg);
};

}  // namespace wargame
