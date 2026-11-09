
#include "pathfinding/AStar.h"

#include "world/World.h"

#include <algorithm>

namespace wargame {

void AStar::prepare(const World& w) {
    const size_t n = static_cast<size_t>(w.cellCount());
    if (g_.size() != n) {
        g_.assign(n, 0.0);
        parent_.assign(n, -1);
        seen_.assign(n, 0);
        closed_.assign(n, 0);
        stamp_ = 0;
    }

    ++stamp_;
    if (stamp_ == 0) {
        // The stamp wrapped after 4 billion searches. Clear once so stale tags
        // from the previous cycle cannot be mistaken for fresh ones.
        std::fill(seen_.begin(), seen_.end(), 0);
        stamp_ = 1;
    }

    while (!open_.empty()) {
        open_.pop();
    }
    lastExpanded_ = 0;
}

bool AStar::findPath(const World& w, const GridPos& from, const GridPos& to,
                     std::vector<GridPos>& path) {
    path.clear();

    if (!w.inBounds(from) || !w.inBounds(to)) {
        return false;
    }
    if (!w.at(to).passableForLand()) {
        return false;
    }
    if (from == to) {
        return true;  // already there; an empty path is the correct answer
    }

    prepare(w);

    const std::int32_t start = w.index(from);
    const std::int32_t goal = w.index(to);

    seen_[static_cast<size_t>(start)] = stamp_;
    closed_[static_cast<size_t>(start)] = 0;
    g_[static_cast<size_t>(start)] = 0.0;
    parent_[static_cast<size_t>(start)] = -1;
    open_.push({w.octileDistance(from, to), start});

    std::vector<GridPos> neighbors;
    neighbors.reserve(8);

    while (!open_.empty()) {
        const OpenNode node = open_.top();
        open_.pop();

        const size_t ci = static_cast<size_t>(node.cell);
        if (seen_[ci] == stamp_ && closed_[ci]) {
            continue;  // already expanded through a cheaper route
        }
        closed_[ci] = 1;
        ++lastExpanded_;

        if (node.cell == goal) {
            // Walk the parents back, then reverse — the path is wanted from
            // the start, and the chain runs the other way.
            for (std::int32_t c = goal; c != start;
                 c = parent_[static_cast<size_t>(c)]) {
                path.push_back(w.locOf(c));
            }
            std::reverse(path.begin(), path.end());
            return true;
        }

        const GridPos here = w.locOf(node.cell);
        w.mooreNeighbors(here, 1, neighbors);

        for (const GridPos& n : neighbors) {
            if (!w.at(n).passableForLand()) {
                continue;
            }
            const std::int32_t ni = w.index(n);
            const size_t nu = static_cast<size_t>(ni);

            const double tentative = g_[ci] + w.stepCost(here, n);

            const bool fresh = (seen_[nu] != stamp_);
            if (fresh) {
                seen_[nu] = stamp_;
                closed_[nu] = 0;
                g_[nu] = tentative;
                parent_[nu] = node.cell;
            } else if (tentative < g_[nu]) {
                g_[nu] = tentative;
                parent_[nu] = node.cell;
            } else {
                continue;  // no improvement
            }

            open_.push({tentative + w.octileDistance(n, to), ni});
        }
    }

    return false;  // the goal is unreachable
}

}  // namespace wargame
