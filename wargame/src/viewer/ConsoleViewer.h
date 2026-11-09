// Redraws the map in the terminal as the run proceeds.
//
// Works on any platform and needs nothing installed, so it is the fallback
// whenever the windowed viewer is unavailable.

#pragma once

#include "viewer/Viewer.h"

#include <cstdint>
#include <string>

namespace wargame {

class ConsoleViewer : public Viewer {
  public:
    explicit ConsoleViewer(std::int32_t maxWidth) : maxWidth_(maxWidth) {}

    void begin(const Simulation& sim) override;
    void frame(const Simulation& sim) override;
    void end(const Simulation& sim) override;

  private:
    void draw(const Simulation& sim, const char* note);

    std::int32_t maxWidth_ = 0;
    std::int32_t frames_ = 0;
};

}  // namespace wargame
