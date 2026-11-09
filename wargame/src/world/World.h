// The grid the simulation plays on.
//
// The map is divided into boxes of sim.box_km (4 km) a side. Every range in the
// model is expressed in boxes, so the grid is the common scale shared by the
// land, air and naval domains.
//
// The pilot runs on completely flat ground — the other terrain types are
// declared so the type is stable when real terrain arrives, but nothing
// generates them yet.

#pragma once

#include "world/GridPos.h"
#include "world/Side.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wargame {

class Config;

enum class Terrain : std::uint8_t {
    Flat = 0,      // the only type the pilot uses
    Hill = 1,      // slows movement (not modelled yet)
    Mountain = 2,  // impassable (not modelled yet)
    Water = 3,     // land units cannot enter (not modelled yet)
};

// One box of the map. Deliberately a small POD: no back-pointer to the World,
// no bit-packed value field. 6 bytes, so a 64 x 64 grid is 24 KB and stays in
// cache while the event loop runs.
struct Cell {
    Terrain terrain = Terrain::Flat;
    // How many units of each side stand in this box. Which units they are is
    // kept in the World's occupancy index, not here.
    std::uint16_t blueCount = 0;
    std::uint16_t redCount = 0;

    bool passableForLand() const {
        return terrain != Terrain::Mountain && terrain != Terrain::Water;
    }
};

class World {
  public:
    World() = default;

    // Builds the grid from world.rows, world.cols, world.terrain and
    // sim.box_km. Throws ConfigError if any of those is missing.
    void init(const Config& cfg);

    std::int32_t rows() const { return rows_; }
    std::int32_t cols() const { return cols_; }
    std::int32_t cellCount() const { return rows_ * cols_; }
    double boxKm() const { return boxKm_; }

    bool inBounds(const GridPos& l) const {
        return l.row >= 0 && l.row < rows_ && l.col >= 0 && l.col < cols_;
    }

    // Row-major flat index. Callers that already hold a valid GridPos use this to
    // avoid carrying two coordinates around.
    std::int32_t index(const GridPos& l) const { return l.row * cols_ + l.col; }
    GridPos locOf(std::int32_t index) const {
        return GridPos(index / cols_, index % cols_);
    }

    // Unchecked access — the caller has already tested inBounds(). Used on the
    // hot path.
    const Cell& at(const GridPos& l) const { return cells_[index(l)]; }
    Cell& at(const GridPos& l) { return cells_[index(l)]; }

    const Cell& at(std::int32_t index) const { return cells_[index]; }
    Cell& at(std::int32_t index) { return cells_[index]; }

    // Moore neighbourhood: every box within `radius` GRID STEPS, the centre
    // excluded — a square block. This is a grid primitive, not the model's
    // notion of range; for a weapon or a sensor use boxesWithinRange().
    //
    // The result is written into `out`, which is cleared first and whose
    // capacity the caller is expected to keep between calls — this runs on the
    // sensing path and must not allocate every time.
    void mooreNeighbors(const GridPos& centre, std::int32_t radius,
                        std::vector<GridPos>& out) const;

    // Every box whose octile distance from the centre is <= rangeBoxes, the
    // centre excluded. THIS is the model's range: what a weapon with a range of
    // n boxes reaches, and what a sensor with a range of n boxes sees.
    void boxesWithinRange(const GridPos& centre, double rangeBoxes,
                          std::vector<GridPos>& out) const;

    // ---- the two distances, which must not be confused ----

    // Octile distance, in box-equivalents: max + (sqrt(2) - 1) * min.
    //
    // A diagonal step costs sim.diagonal_cost (sqrt(2) by default) because the
    // diagonal of a 4 km box really is 5.66 km. This is the model's distance
    // metric, so "short range = 5 boxes = 20 km" holds in every direction, as
    // the supervisor stated it. Setting sim.diagonal_cost to 1.0 turns this
    // back into Chebyshev without touching the code.
    double octileDistance(const GridPos& a, const GridPos& b) const;

    // Cost of a single step between two adjacent boxes: 1 for a straight move,
    // sim.diagonal_cost for a diagonal one. Used as the edge weight by A*.
    double stepCost(const GridPos& from, const GridPos& to) const;

    double diagonalCost() const { return diagonalCost_; }

    // A compact picture of the grid for eyeballing it during development:
    // terrain by default, or the occupying side where units are present.
    // Large grids are sampled down to fit `maxWidth` columns.
    std::string ascii(std::int32_t maxWidth) const;

    std::string summary() const;

    // ---- occupancy ----
    //
    // Which units stand in which box. Held as an intrusive singly-linked list:
    // one "first occupant" slot per cell and one "next occupant" slot per
    // entity. Insertion is constant time and nothing allocates after setup. The
    // obvious alternative, a vector per cell, would mean one heap allocation
    // per box — 4,096 of them on this grid.

    // Sizes the per-entity table. Safe to call again as entities are added.
    void reserveEntities(std::int32_t count);

    void addOccupant(const GridPos& at, std::int32_t entityId, Side side);
    void removeOccupant(const GridPos& at, std::int32_t entityId, Side side);
    void moveOccupant(const GridPos& from, const GridPos& to, std::int32_t entityId,
                      Side side);

    // Where a side's units sit on average, and how many there are. Kept as
    // running sums updated on every add and remove, so asking costs nothing —
    // scanning 4,096 boxes each time a unit decided what to do would not.
    std::int32_t sideCount(Side s) const;
    bool sideCentre(Side s, GridPos& out) const;

    // Calls f(entityId) for every occupant of the box. f must not add or remove
    // occupants of this box while it runs.
    template <typename F>
    void forEachOccupant(const GridPos& at, F&& f) const {
        for (std::int32_t id = cellHead_[static_cast<size_t>(index(at))];
             id >= 0; id = nextOccupant_[static_cast<size_t>(id)]) {
            f(id);
        }
    }

  private:
    std::int32_t rows_ = 0;
    std::int32_t cols_ = 0;
    double boxKm_ = 0.0;
    double diagonalCost_ = 0.0;  // sim.diagonal_cost
    std::vector<Cell> cells_;    // row-major, one allocation

    std::vector<std::int32_t> cellHead_;      // per cell: first occupant, or -1
    std::vector<std::int32_t> nextOccupant_;  // per entity: next in its cell

    // Running totals behind sideCentre(), indexed by Side.
    std::array<std::int64_t, 3> sumRow_{};
    std::array<std::int64_t, 3> sumCol_{};
    std::array<std::int32_t, 3> count_{};
};

}  // namespace wargame
