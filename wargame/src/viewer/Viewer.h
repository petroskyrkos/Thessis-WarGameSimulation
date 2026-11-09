// Watching a run as it happens.
//
// The engine knows nothing about how a run is displayed: it holds a pointer to
// this interface and calls it at a fixed interval of simulation time, or not at
// all. That separation matters for more than tidiness — the supervisor was
// explicit that there must be no I/O inside the event loop while performance is
// being measured. A benchmark run simply has no viewer attached, and is then
// byte-for-byte the same run as a watched one.

#pragma once

#include "engine/Event.h"

#include <memory>
#include <string>

namespace wargame {

class Config;
class Simulation;

class Viewer {
  public:
    virtual ~Viewer() = default;

    // Called once before the first event.
    virtual void begin(const Simulation& sim) = 0;

    // Called every frameInterval() ticks of simulation time.
    virtual void frame(const Simulation& sim) = 0;

    // Called once when the run ends.
    virtual void end(const Simulation& sim) = 0;

    // True when the viewer wants the run to stop — the window was closed, for
    // instance. The engine checks this between events.
    virtual bool stopRequested() const { return false; }

    // How much simulation time passes between frames.
    Tick frameInterval() const { return frameTicks_; }
    void setFrameInterval(Tick t) { frameTicks_ = t; }

    // Simulation seconds to play per real second. Zero means "as fast as
    // possible", which is what a benchmark wants.
    double speed() const { return simSecondsPerRealSecond_; }
    void setSpeed(double v) { simSecondsPerRealSecond_ = v; }

  protected:
    Tick frameTicks_ = 1;
    double simSecondsPerRealSecond_ = 0.0;
};

// Builds the viewer named by viz.backend, or nullptr when viz.enabled is false.
// `force` turns it on regardless of the config, for a --live flag.
std::unique_ptr<Viewer> makeViewer(const Config& cfg, const Simulation& sim,
                                   bool force);

}  // namespace wargame
