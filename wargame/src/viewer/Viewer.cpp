#include "viewer/Viewer.h"

#include "config/Config.h"
#include "engine/Simulation.h"
#include "viewer/ConsoleViewer.h"
#include "viewer/Win32Viewer.h"

#include <iostream>

namespace wargame {

namespace {

// Reads a colour written as "#RRGGBB" in the config.
std::uint32_t colourFromConfig(const Config& cfg, const std::string& path) {
    const std::string text = cfg.req<std::string>(path);
    const bool shapeOk =
        text.size() == 7 && text[0] == '#' &&
        text.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
    if (!shapeOk) {
        throw ConfigError(path + " must be a colour written as \"#RRGGBB\", not \"" +
                          text + "\"");
    }
    return static_cast<std::uint32_t>(std::stoul(text.substr(1), nullptr, 16));
}

WindowStyle windowStyleFromConfig(const Config& cfg) {
    WindowStyle s;
    s.cellPixels = cfg.req<std::int32_t>("viz.cell_pixels");
    s.statusHeightPixels = cfg.req<std::int32_t>("viz.status_height_pixels");
    s.gutterMinCellPixels = cfg.req<std::int32_t>("viz.gutter_min_cell_pixels");
    s.closePollMs = cfg.req<std::int32_t>("viz.close_poll_ms");

    if (s.cellPixels <= 0) {
        throw ConfigError("viz.cell_pixels must be positive");
    }
    if (s.statusHeightPixels <= 0) {
        throw ConfigError("viz.status_height_pixels must be positive");
    }
    if (s.closePollMs < 0) {
        throw ConfigError("viz.close_poll_ms cannot be negative");
    }

    s.ground = colourFromConfig(cfg, "viz.colors.ground");
    s.grid = colourFromConfig(cfg, "viz.colors.grid");
    s.blue = colourFromConfig(cfg, "viz.colors.blue");
    s.red = colourFromConfig(cfg, "viz.colors.red");
    s.contact = colourFromConfig(cfg, "viz.colors.contact");
    s.statusBackground = colourFromConfig(cfg, "viz.colors.status_background");
    s.statusText = colourFromConfig(cfg, "viz.colors.status_text");
    return s;
}

}  // namespace

std::unique_ptr<Viewer> makeViewer(const Config& cfg, const Simulation& sim,
                                   bool force) {
    const bool enabled = force || cfg.req<bool>("viz.enabled");
    if (!enabled) {
        return nullptr;
    }

    const std::string backend = cfg.req<std::string>("viz.backend");

    std::unique_ptr<Viewer> viewer;
    if (backend == "window") {
        viewer = makeWindowViewer(windowStyleFromConfig(cfg));
        if (viewer == nullptr) {
            std::cout << "viz: no windowed viewer on this platform, "
                         "falling back to the console\n";
        }
    } else if (backend != "console") {
        throw ConfigError("viz.backend must be \"console\" or \"window\", not \"" +
                          backend + "\"");
    }

    if (viewer == nullptr) {
        viewer = std::make_unique<ConsoleViewer>(
            cfg.req<std::int32_t>("viz.console_max_width"));
    }

    // How often to draw, expressed in simulation minutes so the setting means
    // the same thing whatever the tick length is.
    const double frameMinutes = cfg.req<double>("viz.frame_interval_sim_minutes");
    viewer->setFrameInterval(
        std::max<Tick>(1, sim.secondsToTicks(frameMinutes * 60.0)));

    // Playback speed. The run itself takes well under a second, so without a
    // brake there would be nothing to watch.
    const double hoursPerSecond = cfg.req<double>("viz.sim_hours_per_real_second");
    viewer->setSpeed(hoursPerSecond * 3600.0);

    return viewer;
}

}  // namespace wargame
