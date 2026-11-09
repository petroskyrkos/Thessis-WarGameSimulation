// The three resources every dynamic unit carries.
//
// Settled in the 17/10 meeting. Each resource is driven by something different,
// which is why they are kept apart rather than pooled into one "supply" number.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wargame {

enum class Resource : std::uint8_t {
    Munitions = 0,  // event-driven: spent in engagements
    Fuel = 1,       // effort-driven: spent moving
    Supplies = 2,   // time-driven: food and medicine, spent whatever happens
    Count = 3,
};

constexpr std::size_t kResourceCount = static_cast<std::size_t>(Resource::Count);

const char* resourceName(Resource r);

// Position of a resource in the per-unit arrays (capacity, pool, base rate).
inline std::size_t resourceIndex(Resource r) { return static_cast<std::size_t>(r); }

}  // namespace wargame
