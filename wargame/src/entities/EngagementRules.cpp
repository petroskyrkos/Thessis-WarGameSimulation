#include "entities/EngagementRules.h"

#include "config/Config.h"

#include <algorithm>
#include <string>

namespace wargame {

double EngagementRules::hitProbability(double base, double ewCoverage) const {
    const double reduction = ewReduction * ewCoverage;
    const double p = (ewMode == EwMode::Multiplicative) ? base * (1.0 - reduction)
                                                        : base - reduction;
    return std::clamp(p, 0.0, 1.0);
}

EngagementRules EngagementRules::load(const Config& cfg) {
    EngagementRules r;
    r.ewReduction = cfg.req<double>("damage.ew_reduction");
    if (r.ewReduction < 0.0 || r.ewReduction > 1.0) {
        throw ConfigError("damage.ew_reduction must be between 0 and 1");
    }

    const auto mode = cfg.req<std::string>("damage.ew_mode");
    if (mode == "multiplicative") {
        r.ewMode = EwMode::Multiplicative;
    } else if (mode == "subtractive") {
        r.ewMode = EwMode::Subtractive;
    } else {
        throw ConfigError(
            "damage.ew_mode must be \"multiplicative\" or \"subtractive\", not \"" +
            mode + "\"");
    }
    return r;
}

}  // namespace wargame
