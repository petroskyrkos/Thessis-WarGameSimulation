// A position on the grid, in whole boxes.
//
// One box is sim.box_km across (4 km as settled with the supervisor, still
// under review). Distances used by the model are therefore counted in boxes,
// and converted to km only where a real-world figure has to be compared.

#pragma once

#include <cstdint>
#include <string>

namespace wargame {

struct GridPos {
    std::int32_t row = -1;
    std::int32_t col = -1;

    GridPos() = default;
    GridPos(std::int32_t r, std::int32_t c) : row(r), col(c) {}

    friend bool operator==(const GridPos& a, const GridPos& b) {
        return a.row == b.row && a.col == b.col;
    }
    friend bool operator!=(const GridPos& a, const GridPos& b) { return !(a == b); }

    std::string str() const {
        return "(" + std::to_string(row) + "," + std::to_string(col) + ")";
    }
};

}  // namespace wargame
