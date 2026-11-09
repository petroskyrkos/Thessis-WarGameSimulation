// Checks of the engagement rules: hit probability and electronic warfare (EW).
//
// Two kinds of check. The formula is tested directly against figures worked out
// by hand. The behaviour is tested by running the pilot battle with the config
// varied (hit probability 1 or 0, full EW cover) and comparing the share of
// shots that landed with what the rules say it should be.

#include "config/Config.h"
#include "engine/Simulation.h"
#include "entities/EngagementRules.h"
#include "entities/UnitProfile.h"
#include "scenarios/PlainBattle.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wargame::selfcheck {

namespace {

struct Fight {
    RunStats stats;
    std::int64_t shots = 0;
    std::int64_t hits = 0;
    std::int64_t kills = 0;

    double hitRate() const {
        return shots > 0 ? static_cast<double>(hits) / static_cast<double>(shots)
                         : 0.0;
    }
};

// Runs the pilot battle once with the given config.
Fight fight(const Config& cfg) {
    Simulation sim;
    sim.init(cfg);

    PlainBattle battle;
    battle.build(sim, cfg);

    Fight f;
    f.stats = sim.run();
    for (const Side side : {Side::Blue, Side::Red}) {
        const SideReport r = battle.report(side);
        f.shots += r.shots;
        f.hits += r.hits;
        f.kills += r.kills;
    }
    return f;
}

// A copy of the config with one value changed.
Config varied(const Config& cfg, const std::string& path, const nlohmann::json& v) {
    Config c = cfg;
    c.set(path, v);
    return c;
}

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

}  // namespace

void runEngagementChecks(const Config& cfg) {
    constexpr const char* kProfile = "units.infantry_battalion";
    const std::string hitPath = std::string(kProfile) + ".hit_probability";
    const std::string coverPath = std::string(kProfile) + ".ew_coverage";

    // ---- the formula, against figures worked out by hand ----
    {
        EngagementRules mult;
        mult.ewReduction = 0.35;
        mult.ewMode = EwMode::Multiplicative;
        EngagementRules sub = mult;
        sub.ewMode = EwMode::Subtractive;

        const bool formulaOk =
            // no EW cover: the base probability, in both modes
            near(mult.hitProbability(0.925, 0.0), 0.925, 1e-9) &&
            near(sub.hitProbability(0.925, 0.0), 0.925, 1e-9) &&
            // full cover: 0.925 * 0.65 and 0.925 - 0.35
            near(mult.hitProbability(0.925, 1.0), 0.60125, 1e-9) &&
            near(sub.hitProbability(0.925, 1.0), 0.575, 1e-9) &&
            // half cover halves the reduction
            near(mult.hitProbability(0.925, 0.5), 0.763125, 1e-9) &&
            near(sub.hitProbability(0.925, 0.5), 0.75, 1e-9) &&
            // subtracting more than there is leaves 0, never a negative chance
            near(sub.hitProbability(0.30, 1.0), 0.0, 1e-9) &&
            // and a probability never exceeds 1
            near(mult.hitProbability(1.0, 0.0), 1.0, 1e-9);
        report("the hit probability formula gives the hand-worked values", formulaOk);
    }

    // ---- an unknown EW mode must be refused, not guessed ----
    {
        bool refused = false;
        try {
            EngagementRules::load(varied(cfg, "damage.ew_mode", "sideways"));
        } catch (const ConfigError&) {
            refused = true;
        }
        report("an unknown damage.ew_mode is rejected", refused);
    }

    // ---- behaviour over a whole battle ----
    const EngagementRules rules = EngagementRules::load(cfg);
    const UnitProfile profile = UnitProfile::load(cfg, kProfile);

    // The tolerance is about three standard errors for the several thousand
    // shots a battle fires; the seed is fixed, so this cannot flake.
    constexpr double kTolerance = 0.035;

    const Fight normal = fight(cfg);
    const double expected = rules.hitProbability(profile.hitProbability,
                                                 profile.ewCoverage);
    report("the share of shots that land matches the configured probability",
           normal.shots > 500 && near(normal.hitRate(), expected, kTolerance));

    const Fight certain = fight(varied(cfg, hitPath, 1.0));
    report("a hit probability of 1 makes every shot land",
           certain.shots > 0 && certain.hits == certain.shots);

    const Fight never = fight(varied(cfg, hitPath, 0.0));
    report("a hit probability of 0 lands no shot and nobody dies",
           never.shots > 0 && never.hits == 0 && never.kills == 0);

    // Full EW cover over both armies, in each mode. The expected rate is worked
    // out from the loaded rules, so it follows the config.
    const double base = profile.hitProbability;
    const double reduction = rules.ewReduction;

    Config coverMult = varied(cfg, coverPath, 1.0);
    coverMult.set("damage.ew_mode", "multiplicative");
    const Fight jammedMult = fight(coverMult);
    report("full EW cover lowers the hit rate multiplicatively",
           jammedMult.shots > 500 &&
               near(jammedMult.hitRate(), base * (1.0 - reduction), kTolerance));

    Config coverSub = varied(cfg, coverPath, 1.0);
    coverSub.set("damage.ew_mode", "subtractive");
    const Fight jammedSub = fight(coverSub);
    report("full EW cover lowers the hit rate subtractively",
           jammedSub.shots > 500 &&
               near(jammedSub.hitRate(), std::max(0.0, base - reduction),
                    kTolerance));
}

}  // namespace wargame::selfcheck
