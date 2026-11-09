// How supply consumption surges after a battle.
//
// From the 17/10 meeting: after a battle, supplies (food and medicine) are
// consumed at 200% of the base rate for one week, and a further battle inside
// that window adds another week on top of what remains.

#pragma once

namespace wargame {

class Config;

struct PostCombatSurge {
    double multiplier = 1.0;
    double days = 0.0;
    bool cumulative = true;

    static PostCombatSurge load(const Config& cfg);
};

}  // namespace wargame
