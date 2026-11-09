// A snapshot of every unit, written to CSV at a fixed interval of simulation time.
//
// For the visualisation that comes later: it can replay a run from these rows
// without running the simulation again. It is a Viewer, so the engine treats it
// like any other observer and the run itself is unchanged by it (same checksum).
// Never attach it to a measured run: it writes files inside the loop.

#pragma once

#include "viewer/Viewer.h"

#include <fstream>
#include <string>

namespace wargame {

class PlainBattle;

class StateDump : public Viewer {
  public:
    // Opens `path` (creating its folder). Throws ConfigError if it cannot.
    StateDump(const PlainBattle& battle, const std::string& path, Tick interval);

    void begin(const Simulation& sim) override;
    void frame(const Simulation& sim) override;
    void end(const Simulation& sim) override;

    const std::string& path() const { return path_; }
    std::uint64_t rows() const { return rows_; }

  private:
    void writeAll(const Simulation& sim);

    const PlainBattle& battle_;
    std::string path_;
    std::ofstream out_;
    std::uint64_t rows_ = 0;
};

}  // namespace wargame
