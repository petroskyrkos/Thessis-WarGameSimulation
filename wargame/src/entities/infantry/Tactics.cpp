#include "entities/infantry/Tactics.h"

#include "config/Config.h"

namespace wargame {

Tactics Tactics::load(const Config& cfg, const std::string& path) {
    Tactics t;
    t.decisionSeconds = cfg.req<double>(path + ".decision_interval_seconds");
    t.engagementSeconds = cfg.req<double>(path + ".engagement_interval_seconds");
    t.pursue = cfg.req<bool>(path + ".pursue_after_objective");
    if (t.decisionSeconds <= 0.0 || t.engagementSeconds <= 0.0) {
        throw ConfigError(path + ": decision and engagement intervals must be positive");
    }
    return t;
}

}  // namespace wargame
