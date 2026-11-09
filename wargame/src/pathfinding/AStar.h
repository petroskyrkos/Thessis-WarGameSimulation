// A* path finding on the grid.
//
// Edge weights are the world's own step costs (1 straight, sqrt(2) diagonal)
// and the heuristic is the octile distance, which is admissible and consistent
// with those weights — so the first path found is the cheapest one.
//
// One AStar object is meant to be kept and reused. Its working arrays are
// allocated once and reused across searches, and visited cells are tagged with
// a run stamp instead of being cleared: wiping 4,096 entries before every
// search would cost more than the search itself.

#pragma once

#include "world/GridPos.h"

#include <cstdint>
#include <queue>
#include <vector>

namespace wargame {

class World;

class AStar {
  public:
    // Fills `path` with the boxes to step through, excluding `from` and
    // including `to`. Returns false if no route exists, leaving `path` empty.
    bool findPath(const World& w, const GridPos& from, const GridPos& to,
                  std::vector<GridPos>& path);

    // Boxes expanded by the last search — a cheap measure of how hard the
    // terrain made the problem.
    std::uint64_t lastExpanded() const { return lastExpanded_; }

  private:
    struct OpenNode {
        double f = 0.0;
        std::int32_t cell = -1;
    };
    struct Costlier {
        bool operator()(const OpenNode& a, const OpenNode& b) const {
            // Ties broken by cell index so a search is reproducible rather
            // than dependent on the heap's internal order.
            if (a.f != b.f) {
                return a.f > b.f;
            }
            return a.cell > b.cell;
        }
    };

    void prepare(const World& w);

    std::vector<double> g_;
    std::vector<std::int32_t> parent_;
    std::vector<std::uint32_t> seen_;    // run stamp per cell
    std::vector<std::uint8_t> closed_;   // valid only where seen_ == stamp_
    std::uint32_t stamp_ = 0;
    std::uint64_t lastExpanded_ = 0;

    std::priority_queue<OpenNode, std::vector<OpenNode>, Costlier> open_;
};

}  // namespace wargame
