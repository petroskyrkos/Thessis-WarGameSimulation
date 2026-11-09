#include "world/World.h"

#include "config/Config.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace wargame {

namespace {

Terrain terrainFromName(const std::string& name) {
    if (name == "flat") return Terrain::Flat;
    if (name == "hill") return Terrain::Hill;
    if (name == "mountain") return Terrain::Mountain;
    if (name == "water") return Terrain::Water;
    throw ConfigError("world.terrain: unknown terrain \"" + name +
                      "\" (expected flat, hill, mountain or water)");
}

char terrainGlyph(Terrain t) {
    switch (t) {
        case Terrain::Flat: return '.';
        case Terrain::Hill: return 'n';
        case Terrain::Mountain: return '^';
        case Terrain::Water: return '~';
    }
    return '?';
}

}  // namespace

void World::init(const Config& cfg) {
    const auto rows = cfg.req<std::int32_t>("world.rows");
    const auto cols = cfg.req<std::int32_t>("world.cols");
    const auto boxKm = cfg.req<double>("sim.box_km");
    const auto diagonalCost = cfg.req<double>("sim.diagonal_cost");
    const auto terrainName = cfg.req<std::string>("world.terrain");

    if (rows <= 0 || cols <= 0) {
        throw ConfigError("world.rows and world.cols must both be positive");
    }
    if (boxKm <= 0.0) {
        throw ConfigError("sim.box_km must be positive");
    }
    // Below 1 a diagonal would be cheaper than a straight step; above sqrt(2)
    // it would cost more than the ground it covers. Both are nonsense.
    if (diagonalCost < 1.0 || diagonalCost > 1.4142135624) {
        throw ConfigError(
            "sim.diagonal_cost must be between 1.0 (Chebyshev) and "
            "1.4142135624 (true octile)");
    }

    rows_ = rows;
    cols_ = cols;
    boxKm_ = boxKm;
    diagonalCost_ = diagonalCost;

    // The pilot's ground is uniform, so one fill is the whole terrain model.
    // Real terrain will replace this with a generator or a map file.
    Cell c;
    c.terrain = terrainFromName(terrainName);
    cells_.assign(static_cast<size_t>(rows_) * static_cast<size_t>(cols_), c);

    cellHead_.assign(cells_.size(), -1);
    nextOccupant_.clear();
    sumRow_.fill(0);
    sumCol_.fill(0);
    count_.fill(0);
}

std::int32_t World::sideCount(Side s) const {
    return count_[static_cast<size_t>(s)];
}

bool World::sideCentre(Side s, GridPos& out) const {
    const size_t i = static_cast<size_t>(s);
    if (count_[i] <= 0) {
        return false;
    }
    out = GridPos(static_cast<std::int32_t>(sumRow_[i] / count_[i]),
              static_cast<std::int32_t>(sumCol_[i] / count_[i]));
    return true;
}

void World::reserveEntities(std::int32_t count) {
    if (count > static_cast<std::int32_t>(nextOccupant_.size())) {
        nextOccupant_.resize(static_cast<size_t>(count), -1);
    }
}

void World::addOccupant(const GridPos& at, std::int32_t entityId, Side side) {
    reserveEntities(entityId + 1);

    const size_t cell = static_cast<size_t>(index(at));
    nextOccupant_[static_cast<size_t>(entityId)] = cellHead_[cell];
    cellHead_[cell] = entityId;

    Cell& c = cells_[cell];
    if (side == Side::Blue) {
        ++c.blueCount;
    } else if (side == Side::Red) {
        ++c.redCount;
    }

    const size_t si = static_cast<size_t>(side);
    sumRow_[si] += at.row;
    sumCol_[si] += at.col;
    ++count_[si];
}

void World::removeOccupant(const GridPos& at, std::int32_t entityId, Side side) {
    const size_t cell = static_cast<size_t>(index(at));

    std::int32_t prev = -1;
    std::int32_t cur = cellHead_[cell];
    while (cur >= 0 && cur != entityId) {
        prev = cur;
        cur = nextOccupant_[static_cast<size_t>(cur)];
    }
    if (cur < 0) {
        return;  // not here; nothing to unlink
    }

    const std::int32_t next = nextOccupant_[static_cast<size_t>(cur)];
    if (prev < 0) {
        cellHead_[cell] = next;
    } else {
        nextOccupant_[static_cast<size_t>(prev)] = next;
    }
    nextOccupant_[static_cast<size_t>(cur)] = -1;

    Cell& c = cells_[cell];
    if (side == Side::Blue && c.blueCount > 0) {
        --c.blueCount;
    } else if (side == Side::Red && c.redCount > 0) {
        --c.redCount;
    }

    const size_t si = static_cast<size_t>(side);
    if (count_[si] > 0) {
        sumRow_[si] -= at.row;
        sumCol_[si] -= at.col;
        --count_[si];
    }
}

void World::moveOccupant(const GridPos& from, const GridPos& to, std::int32_t entityId,
                         Side side) {
    if (from == to) {
        return;
    }
    removeOccupant(from, entityId, side);
    addOccupant(to, entityId, side);
}

void World::mooreNeighbors(const GridPos& centre, std::int32_t radius,
                           std::vector<GridPos>& out) const {
    out.clear();
    if (radius <= 0) {
        return;
    }

    // Clamp the scan to the grid instead of testing every candidate, so the
    // inner loop has no bounds check at all.
    const std::int32_t rLo = std::max<std::int32_t>(0, centre.row - radius);
    const std::int32_t rHi = std::min<std::int32_t>(rows_ - 1, centre.row + radius);
    const std::int32_t cLo = std::max<std::int32_t>(0, centre.col - radius);
    const std::int32_t cHi = std::min<std::int32_t>(cols_ - 1, centre.col + radius);

    for (std::int32_t r = rLo; r <= rHi; ++r) {
        for (std::int32_t c = cLo; c <= cHi; ++c) {
            if (r == centre.row && c == centre.col) {
                continue;  // the centre is not its own neighbour
            }
            out.emplace_back(r, c);
        }
    }
}

void World::boxesWithinRange(const GridPos& centre, double rangeBoxes,
                             std::vector<GridPos>& out) const {
    out.clear();
    if (rangeBoxes <= 0.0) {
        return;
    }

    // A box at octile distance d is at most d grid steps away, so scanning the
    // square block of that many steps cannot miss one.
    const auto steps = static_cast<std::int32_t>(std::floor(rangeBoxes));

    const std::int32_t rLo = std::max<std::int32_t>(0, centre.row - steps);
    const std::int32_t rHi = std::min<std::int32_t>(rows_ - 1, centre.row + steps);
    const std::int32_t cLo = std::max<std::int32_t>(0, centre.col - steps);
    const std::int32_t cHi = std::min<std::int32_t>(cols_ - 1, centre.col + steps);

    // A small tolerance, so a range of exactly 5 still includes the box whose
    // distance lands on 4.999999999 through floating point.
    const double limit = rangeBoxes + 1e-9;

    for (std::int32_t r = rLo; r <= rHi; ++r) {
        for (std::int32_t c = cLo; c <= cHi; ++c) {
            if (r == centre.row && c == centre.col) {
                continue;
            }
            if (octileDistance(centre, GridPos(r, c)) <= limit) {
                out.emplace_back(r, c);
            }
        }
    }
}

double World::octileDistance(const GridPos& a, const GridPos& b) const {
    const std::int32_t dr = std::abs(a.row - b.row);
    const std::int32_t dc = std::abs(a.col - b.col);
    const std::int32_t hi = std::max(dr, dc);
    const std::int32_t lo = std::min(dr, dc);
    // hi - lo straight steps plus lo diagonal ones.
    return static_cast<double>(hi - lo) +
           diagonalCost_ * static_cast<double>(lo);
}

double World::stepCost(const GridPos& from, const GridPos& to) const {
    const std::int32_t dr = std::abs(from.row - to.row);
    const std::int32_t dc = std::abs(from.col - to.col);
    return (dr != 0 && dc != 0) ? diagonalCost_ : 1.0;
}

std::string World::ascii(std::int32_t maxWidth) const {
    if (cells_.empty()) {
        return "(world not initialised)\n";
    }

    // Sample every nth box so a large grid still prints as a readable block.
    const std::int32_t step =
        std::max<std::int32_t>(1, (cols_ + maxWidth - 1) / maxWidth);

    std::ostringstream os;
    for (std::int32_t r = 0; r < rows_; r += step) {
        for (std::int32_t c = 0; c < cols_; c += step) {
            const Cell& cell = at(GridPos(r, c));
            if (cell.blueCount > 0 && cell.redCount > 0) {
                os << 'X';  // both sides in the same box: contact
            } else if (cell.blueCount > 0) {
                os << 'B';
            } else if (cell.redCount > 0) {
                os << 'R';
            } else {
                os << terrainGlyph(cell.terrain);
            }
        }
        os << '\n';
    }

    if (step > 1) {
        os << "(sampled every " << step << " boxes)\n";
    }
    return os.str();
}

std::string World::summary() const {
    std::ostringstream os;
    os << rows_ << " x " << cols_ << " boxes of " << boxKm_ << " km = "
       << (rows_ * boxKm_) << " x " << (cols_ * boxKm_) << " km, "
       << cellCount() << " cells, "
       << (cells_.size() * sizeof(Cell) / 1024.0) << " KB, diagonal costs "
       << diagonalCost_;
    return os.str();
}

}  // namespace wargame
