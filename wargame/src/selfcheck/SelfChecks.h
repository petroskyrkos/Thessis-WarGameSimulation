// The program's built-in self-checks.
//
// They run when the program is started with no flags, before anything is
// measured. A failing check means the engine or the model is broken, so no
// benchmark number taken from it can be trusted.

#pragma once

namespace wargame {
class Config;
class World;
}  // namespace wargame

namespace wargame::selfcheck {

void runGridChecks(const World& world);
void runEngineChecks(const Config& cfg);
void runUnitChecks(const Config& cfg);
void runBattleChecks(const Config& cfg);
void runEngagementChecks(const Config& cfg);
void runBenchmarkChecks(const Config& cfg);

// Not part of the start-up checks (it runs a block of battles, so it takes a
// while): started with the --symmetry flag. Returns true when no side is favoured.
bool runSymmetryCheck(const Config& cfg);

}  // namespace wargame::selfcheck
