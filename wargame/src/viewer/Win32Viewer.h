// A real window showing the battle as it is fought.
//
// Built straight on the Windows API (user32 and gdi32) rather than on SFML,
// SDL or raylib, so there is nothing to install: MSVC and MinGW both ship what
// this needs. The rest of the project has no third-party dependency beyond a
// vendored JSON header, and the viewer keeps it that way.
//
// On anything other than Windows this file compiles to nothing and the factory
// falls back to the console viewer.

#pragma once

#include "viewer/Viewer.h"

#include <cstdint>
#include <memory>

namespace wargame {

// How the window looks and behaves. Every field comes from the viz section of
// the config; nothing here has a default.
struct WindowStyle {
    std::int32_t cellPixels = 0;           // pixels per box
    std::int32_t statusHeightPixels = 0;   // height of the status strip
    std::int32_t gutterMinCellPixels = 0;  // boxes smaller than this get no outline
    std::int32_t closePollMs = 0;          // wait between checks once the run ends

    // Colours as 0x00RRGGBB.
    std::uint32_t ground = 0;
    std::uint32_t grid = 0;
    std::uint32_t blue = 0;
    std::uint32_t red = 0;
    std::uint32_t contact = 0;
    std::uint32_t statusBackground = 0;
    std::uint32_t statusText = 0;
};

// Returns nullptr when no windowed viewer can be created on this platform.
std::unique_ptr<Viewer> makeWindowViewer(const WindowStyle& style);

}  // namespace wargame
