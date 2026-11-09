// Which force something belongs to.
//
// Blue is the defender (Greece), Red the attacker — the standard war-game
// convention, not flags, as the supervisor put it.
//
// This sits in its own header because both the grid and the entities need it,
// and neither should have to include the other to get it.

#pragma once

#include <cstdint>

namespace wargame {

enum class Side : std::uint8_t {
    None = 0,  // terrain features and other unaligned objects
    Blue = 1,
    Red = 2,
};

const char* sideName(Side s);

}  // namespace wargame
