#include "viewer/ConsoleViewer.h"

#include "engine/Simulation.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace wargame {

namespace {

// Counts what is standing on the map right now. Cheap enough at a frame rate
// of a few per second, and it keeps the viewer from needing to know anything
// about scenarios or unit types.
struct Census {
    std::int32_t blue = 0;
    std::int32_t red = 0;
    std::int32_t contested = 0;
};

Census census(const World& w) {
    Census c;
    for (std::int32_t i = 0; i < w.cellCount(); ++i) {
        const Cell& cell = w.at(i);
        c.blue += cell.blueCount;
        c.red += cell.redCount;
        if (cell.blueCount > 0 && cell.redCount > 0) {
            ++c.contested;
        }
    }
    return c;
}

}  // namespace

void ConsoleViewer::begin(const Simulation& sim) {
    std::cout << "\nlive view - console. B blue, R red, X in contact.\n";
    draw(sim, "start");
}

void ConsoleViewer::frame(const Simulation& sim) {
    ++frames_;
    draw(sim, nullptr);
}

void ConsoleViewer::end(const Simulation& sim) {
    draw(sim, "end");
    std::cout << "\n" << frames_ << " frames drawn.\n";
}

void ConsoleViewer::draw(const Simulation& sim, const char* note) {
    const World& w = sim.world();
    const Census c = census(w);

    const double days = sim.ticksToSeconds(sim.now()) / 86400.0;

    std::ostringstream os;
    // Move the cursor home rather than clearing, so the picture does not
    // flicker between frames.
    os << "\033[H\033[2J";
    os << "day " << std::fixed << std::setprecision(2) << days << "   tick "
       << sim.now() << "   blue " << c.blue << "   red " << c.red
       << "   boxes in contact " << c.contested;
    if (note != nullptr) {
        os << "   [" << note << "]";
    }
    os << "\n\n" << w.ascii(maxWidth_);

    std::cout << os.str() << std::flush;
}

}  // namespace wargame
