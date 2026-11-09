// Checks of the grid: indexing, neighbourhoods, and the octile range metric.

#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"
#include "world/World.h"

#include <cmath>
#include <vector>

namespace wargame::selfcheck {

void runGridChecks(const wargame::World& w) {
    // Round-tripping a flat index through locOf() must give it back.
    bool roundTrip = true;
    for (std::int32_t i = 0; i < w.cellCount(); ++i) {
        if (w.index(w.locOf(i)) != i) {
            roundTrip = false;
            break;
        }
    }
    report("index <-> location round-trip over every cell", roundTrip);

    std::vector<wargame::GridPos> neighbors;
    const wargame::GridPos middle(w.rows() / 2, w.cols() / 2);

    w.mooreNeighbors(middle, 1, neighbors);
    report("interior box has 8 neighbours at radius 1", neighbors.size() == 8);
    w.mooreNeighbors(middle, 2, neighbors);
    report("interior box has 24 neighbours at radius 2", neighbors.size() == 24);
    w.mooreNeighbors(wargame::GridPos(0, 0), 1, neighbors);
    report("corner box has 3 neighbours at radius 1", neighbors.size() == 3);

    bool neighborsValid = true;
    w.mooreNeighbors(wargame::GridPos(0, 0), 3, neighbors);
    for (const wargame::GridPos& n : neighbors) {
        if (!w.inBounds(n) || n == wargame::GridPos(0, 0)) {
            neighborsValid = false;
            break;
        }
    }
    report("neighbours stay in bounds and exclude the centre", neighborsValid);

    // The range metric. The supervisor's tiers read "short = 5 boxes = 20 km",
    // which holds in every direction only because a diagonal costs sqrt(2).
    const double straight5 = w.octileDistance(wargame::GridPos(10, 10), wargame::GridPos(10, 15));
    const double diag5 = w.octileDistance(wargame::GridPos(10, 10), wargame::GridPos(15, 15));
    report("5 boxes straight costs 5 box-equivalents",
           std::abs(straight5 - 5.0) < 1e-9);
    report("5 boxes diagonal costs 5*sqrt(2) box-equivalents",
           std::abs(diag5 - 5.0 * w.diagonalCost()) < 1e-9);
    report("a diagonal step costs more than a straight one",
           w.stepCost(wargame::GridPos(4, 4), wargame::GridPos(5, 5)) >
               w.stepCost(wargame::GridPos(4, 4), wargame::GridPos(4, 5)));

    std::vector<wargame::GridPos> inRange;
    w.boxesWithinRange(middle, 5.0, inRange);
    w.mooreNeighbors(middle, 5, neighbors);
    report("octile range covers fewer boxes than the square block",
           inRange.size() < neighbors.size());

    bool coverageSound = true;
    for (const wargame::GridPos& l : inRange) {
        if (w.octileDistance(middle, l) > 5.0 + 1e-9) {
            coverageSound = false;
            break;
        }
    }
    report("no box in range exceeds the range", coverageSound);
}

}  // namespace wargame::selfcheck
