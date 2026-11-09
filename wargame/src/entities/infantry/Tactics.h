// How an infantry battalion decides, in time terms.
//
// Separate from UnitProfile because these are tuning knobs for the simulation
// rather than properties of the weapon.

#pragma once

#include <string>

namespace wargame {

class Config;

struct Tactics {
    double decisionSeconds = 0.0;    // how often an idle unit reconsiders
    double engagementSeconds = 0.0;  // how often an engaged unit fires

    // [pilot] When a unit reaches its objective with no enemy in sight, head
    // for the enemy's centre of mass instead of standing still.
    //
    // This assumes a perfect picture at formation level while tactical sight
    // stays at the modelled one box. Without it a battle ends in mutual
    // blindness: infantry sees 4 km, so the survivors of the first clash sit
    // in opposite corners for the rest of the campaign. The real model gets
    // this picture from reconnaissance, drones and higher command, none of
    // which exist yet.
    bool pursue = true;

    static Tactics load(const Config& cfg, const std::string& path);
};

}  // namespace wargame
