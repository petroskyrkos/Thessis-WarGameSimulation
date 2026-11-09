// The base class of everything the simulation tracks.
//
// This mirrors the supervisor's DynamicSimObject: an object with an ID that
// receives events through one virtual handler and may post new events back. The
// state machine of a concrete unit lives behind that handler, exactly as the
// rover's does.
//
// Phase 1 is classical forces, so a unit carries its own full set of
// capabilities. Nothing here fractionates or shares capabilities between
// entities — that is the Mosaic concept and belongs to phase 3.

#pragma once

#include "engine/Event.h"
#include "world/GridPos.h"
#include "world/Side.h"

#include <cstdint>
#include <string>

namespace wargame {

class Simulation;

class Entity {
  public:
    virtual ~Entity() = default;

    // Called by the engine once the entity has been registered, before the run
    // starts. `id` is its index in the engine's entity table.
    void attach(Simulation* sim, std::int32_t id) {
        sim_ = sim;
        id_ = id;
    }

    std::int32_t id() const { return id_; }
    Side side() const { return side_; }
    const GridPos& loc() const { return loc_; }
    bool alive() const { return alive_; }

    // Handles one event. Returning false means the event did not apply, which
    // is not an error — a broadcast reaches entities that have no interest in
    // it.
    virtual bool onEvent(const Event& e) = 0;

    // A short label for the entity's kind, used in logs.
    virtual const char* kind() const = 0;

  protected:
    Simulation* sim_ = nullptr;  // not owned
    std::int32_t id_ = -1;
    Side side_ = Side::None;
    GridPos loc_;
    bool alive_ = true;
};

}  // namespace wargame
