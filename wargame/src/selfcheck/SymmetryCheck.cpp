// Statistical check that the pilot battle does not favour a side.
//
// The two armies are identical and mirror-placed, so over many seeds neither
// should win more often or end with more survivors. One battle proves nothing:
// the result depends on the seed. This runs the same battle over a block of
// consecutive seeds and tests whether the mean difference in survivors (Blue
// minus Red) is distinguishable from zero.
//
// It exists because an asymmetry once went unnoticed: the default seed happened
// to favour Blue, while over 150 seeds the army on the right won about twice as
// often, purely because of how the units were numbered.

#include "config/Config.h"
#include "engine/Simulation.h"
#include "scenarios/PlainBattle.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace wargame::selfcheck {

bool runSymmetryCheck(const Config& cfg) {
    const auto battles = cfg.req<std::int32_t>("selfcheck.symmetry_seeds");
    const auto maxAbsT = cfg.req<double>("selfcheck.symmetry_max_abs_t");
    const auto firstSeed = cfg.req<std::uint64_t>("sim.seed");

    if (battles < 2) {
        throw ConfigError("selfcheck.symmetry_seeds must be at least 2");
    }
    if (maxAbsT <= 0.0) {
        throw ConfigError("selfcheck.symmetry_max_abs_t must be positive");
    }

    std::int32_t blueWins = 0;
    std::int32_t redWins = 0;
    std::int32_t stalemates = 0;   // stopped because nobody could act any more
    std::int32_t ranToTheEnd = 0;  // reached the end of the mission window
    double sum = 0.0;
    double sumSquares = 0.0;

    for (std::int32_t i = 0; i < battles; ++i) {
        Simulation sim;
        sim.init(cfg);
        sim.reseed(firstSeed + static_cast<std::uint64_t>(i));

        PlainBattle battle;
        battle.build(sim, cfg);
        const RunStats stats = sim.run();

        const SideReport blue = battle.report(Side::Blue);
        const SideReport red = battle.report(Side::Red);

        if (blue.alive > 0 && red.alive == 0) {
            ++blueWins;
        } else if (red.alive > 0 && blue.alive == 0) {
            ++redWins;
        }
        if (stats.stoppedEarly &&
            stats.stopReason.rfind("stalemate", 0) == 0) {
            ++stalemates;
        } else if (!stats.stoppedEarly) {
            ++ranToTheEnd;
        }

        const double diff = static_cast<double>(blue.alive - red.alive);
        sum += diff;
        sumSquares += diff * diff;
    }

    const double n = static_cast<double>(battles);
    const double mean = sum / n;
    const double variance = (sumSquares - n * mean * mean) / (n - 1.0);
    const double standardError = std::sqrt(std::max(variance, 0.0) / n);
    const double t = (standardError > 0.0) ? mean / standardError : 0.0;

    std::cout << std::fixed << std::setprecision(1)
              << "  " << battles << " battles over seeds " << firstSeed << ".."
              << (firstSeed + static_cast<std::uint64_t>(battles) - 1) << "\n"
              << "  decided: Blue " << blueWins << ", Red " << redWins
              << "   stopped as a stalemate: " << stalemates
              << "   ran to the mission end: " << ranToTheEnd << "\n"
              << "  survivors, Blue minus Red: mean " << mean << " +/- "
              << standardError << "   t = " << std::setprecision(2) << t
              << "   (limit |t| <= " << maxAbsT << ")\n";

    const bool balanced = std::abs(t) <= maxAbsT;
    report("no side is favoured over the block of seeds", balanced);
    return balanced;
}

}  // namespace wargame::selfcheck
