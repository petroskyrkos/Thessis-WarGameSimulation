#include "entities/PostCombatSurge.h"

#include "config/Config.h"

namespace wargame {

PostCombatSurge PostCombatSurge::load(const Config& cfg) {
    PostCombatSurge s;
    s.multiplier = cfg.req<double>("resources.post_combat.supplies_multiplier");
    s.days = cfg.req<double>("resources.post_combat.days");
    s.cumulative = cfg.req<bool>("resources.post_combat.cumulative");

    if (s.multiplier < 1.0) {
        throw ConfigError(
            "resources.post_combat.supplies_multiplier must be at least 1.0 "
            "(combat cannot reduce consumption)");
    }
    if (s.days < 0.0) {
        throw ConfigError("resources.post_combat.days cannot be negative");
    }
    return s;
}

}  // namespace wargame
