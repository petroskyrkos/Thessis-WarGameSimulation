#include "engine/Simulation.h"

#include "config/Config.h"
#include "log/EventLog.h"
#include "viewer/Viewer.h"

#include <chrono>
#include <thread>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace wargame {

std::string RunStats::str() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3) << "ended at tick " << endTick
       << ", " << eventsProcessed << " events processed, " << dispatchCalls
       << " handler calls, peak queue " << maxQueueSize << ", " << wallSeconds
       << " s wall, " << std::setprecision(0) << eventsPerSecond()
       << " events/s, checksum 0x" << std::hex << checksum << std::dec;
    if (stoppedEarly && !stopReason.empty()) {
        os << " (stopped: " << stopReason << ")";
    }
    return os.str();
}

void Simulation::init(const Config& cfg) {
    world_.init(cfg);

    const auto seed = cfg.req<std::uint64_t>("sim.seed");
    tickSeconds_ = cfg.req<double>("sim.tick_seconds");
    const auto missionDays = cfg.req<double>("sim.mission_days");

    if (tickSeconds_ <= 0.0) {
        throw ConfigError("sim.tick_seconds must be positive");
    }
    if (missionDays <= 0.0) {
        throw ConfigError("sim.mission_days must be positive");
    }

    const auto tieBreak = cfg.req<std::string>("sim.tie_break");
    if (tieBreak == "random") {
        randomTieBreak_ = true;
    } else if (tieBreak == "creation_order") {
        randomTieBreak_ = false;
    } else {
        throw ConfigError(
            "sim.tie_break must be \"random\" or \"creation_order\", not \"" +
            tieBreak + "\"");
    }

    const auto stalemateHours = cfg.req<double>("sim.stalemate_after_hours");
    if (stalemateHours < 0.0) {
        throw ConfigError(
            "sim.stalemate_after_hours cannot be negative (0 turns it off)");
    }
    stalemateTicks_ =
        (stalemateHours > 0.0) ? secondsToTicks(stalemateHours * 3600.0) : 0;

    rng_ = Rng(seed);
    now_ = 0;
    stopRequested_ = false;
    stopReason_.clear();
    endTick_ = daysToTicks(missionDays);
    nextSeq_ = 0;
    lastActivity_ = 0;
    stats_ = RunStats{};
    samples_.clear();
}

Tick Simulation::secondsToTicks(double seconds) const {
    // Round rather than truncate, so a rate expressed in whole minutes or hours
    // does not drift by a tick each time it is converted.
    return static_cast<Tick>(std::llround(seconds / tickSeconds_));
}

std::int32_t Simulation::addEntity(std::unique_ptr<Entity> e) {
    const auto id = static_cast<std::int32_t>(entities_.size());
    e->attach(this, id);
    entities_.push_back(std::move(e));
    return id;
}

Entity* Simulation::entity(std::int32_t id) {
    if (id < 0 || id >= static_cast<std::int32_t>(entities_.size())) {
        return nullptr;
    }
    return entities_[static_cast<size_t>(id)].get();
}

bool Simulation::postEvent(Event e) {
    if (e.time < now_) {
        // An event in the past would break causality. This is a programming
        // error, not a modelling one, so it must be loud.
        throw std::logic_error("Simulation::postEvent: event at tick " +
                               std::to_string(e.time) +
                               " is behind the current tick " +
                               std::to_string(now_));
    }
    if (e.time > endTick_) {
        return false;  // beyond the end of the run; dropped, as in the original
    }

    e.seq = nextSeq_++;
    // A draw from the seeded RNG decides who goes first among events on the same
    // tick, so the outcome does not depend on how units happen to be numbered.
    // In creation_order mode no draw is made and the old behaviour (and its
    // checksums) is reproduced exactly.
    e.tie = randomTieBreak_ ? rng_.next() : 0;
    fev_.push(e);
    ++stats_.eventsPosted;
    if (fev_.size() > stats_.maxQueueSize) {
        stats_.maxQueueSize = fev_.size();
    }
    return true;
}

void Simulation::requestStop(const std::string& reason) {
    if (!stopRequested_) {
        stopRequested_ = true;
        stopReason_ = reason;
    }
}

void Simulation::dispatch(const Event& e) {
    if (e.dest == kBroadcast) {
        // A linear sweep over the entity table. Destroyed entities stay in the
        // table and are skipped.
        for (std::unique_ptr<Entity>& slot : entities_) {
            if (slot && slot->alive()) {
                slot->onEvent(e);
                ++stats_.dispatchCalls;
            }
        }
        return;
    }

    if (Entity* target = entity(e.dest)) {
        if (target->alive()) {
            target->onEvent(e);
            ++stats_.dispatchCalls;
        }
    }
    // An event addressed to an entity that has since been destroyed is simply
    // dropped — a shot already in flight at a target that is already gone.
}

RunStats Simulation::run() {
    const auto wallStart = std::chrono::steady_clock::now();

    now_ = 0;

    if (log_ != nullptr) {
        log_->note(*this, LogEvent::RunStart,
                   "run starts, " + std::to_string(entities_.size()) +
                       " entities");
    }

    // Bracket the run, as the original does.
    postEvent(Event(0, EventType::SimStart, kSimEngine, kBroadcast));
    postEvent(Event(endTick_, EventType::SimStop, kSimEngine, kBroadcast));

    Tick nextFrame = 0;
    Tick nextSitrep = 0;
    auto viewerStart = std::chrono::steady_clock::now();

    if (viewer_ != nullptr) {
        viewerStart = std::chrono::steady_clock::now();
        viewer_->begin(*this);
        stats_.viewerSeconds += std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - viewerStart)
                                    .count();
        nextFrame = viewer_->frameInterval();
    }

    while (!fev_.empty()) {
        const Event e = fev_.top();
        fev_.pop();

        if (e.time > endTick_) {
            break;
        }

        now_ = e.time;
        dispatch(e);

        if (log_ != nullptr) {
            // Every event audited, when asked for: this is what catches a
            // strength that rises, at the exact event that caused it.
            if (log_->auditStrict()) {
                log_->audit(*this, eventTypeName(e.type));
            }
            if (log_->sitrepInterval() > 0 && now_ >= nextSitrep) {
                log_->situation(*this);
                if (!log_->auditStrict()) {
                    log_->audit(*this, "sitrep");
                }
                nextSitrep = now_ + log_->sitrepInterval();
            }
        }

        // Stalemate: nobody has moved or fired for the configured time. What is
        // left in the queue is only units waking up to find nothing to do.
        if (stalemateTicks_ > 0 && now_ - lastActivity_ >= stalemateTicks_) {
            std::ostringstream why;
            why << "stalemate: no unit moved or fired for "
                << std::fixed << std::setprecision(1)
                << ticksToSeconds(now_ - lastActivity_) / 3600.0 << " hours";
            requestStop(why.str());
        }

        if (stopRequested_) {
            ++stats_.eventsProcessed;
            stats_.stoppedEarly = true;
            stats_.stopReason = stopReason_;
            break;
        }

        if (viewer_ != nullptr && now_ >= nextFrame) {
            viewerStart = std::chrono::steady_clock::now();

            // Hold the picture back to the requested playback speed. Without
            // this the whole campaign would flash past in under a second.
            const double speed = viewer_->speed();
            if (speed > 0.0) {
                const double simSeconds = ticksToSeconds(now_);
                const double wantWall = simSeconds / speed;
                // Real time since the run started. Viewer time is NOT taken
                // out here: the sleeps already performed are part of how far
                // playback has got, and subtracting them would make every
                // frame wait the full amount all over again.
                const double haveWall =
                    std::chrono::duration<double>(viewerStart - wallStart)
                        .count();
                const double sleepFor = wantWall - haveWall;
                if (sleepFor > 0.0) {
                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(sleepFor));
                }
            }

            viewer_->frame(*this);
            nextFrame = now_ + viewer_->frameInterval();

            stats_.viewerSeconds +=
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - viewerStart)
                    .count();

            if (viewer_->stopRequested()) {
                stats_.stoppedEarly = true;
                break;
            }
        }

        ++stats_.eventsProcessed;

        if (sampleEvery_ > 0 && stats_.eventsProcessed % sampleEvery_ == 0) {
            const double sinceStart = std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() - wallStart)
                                          .count();
            samples_.push_back(RunSample{stats_.eventsProcessed, now_,
                                         sinceStart - stats_.viewerSeconds});
        }

        // Order-sensitive mix (FNV-1a style) over the fields that define what
        // happened. If two runs of the same scenario disagree here, the engine
        // is not deterministic.
        std::uint64_t h = stats_.checksum;
        const auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<std::uint64_t>(e.time));
        mix(static_cast<std::uint64_t>(e.type));
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.src)));
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.dest)));
        stats_.checksum = h;
    }

    stats_.endTick = now_;

    if (log_ != nullptr) {
        log_->situation(*this);
        log_->audit(*this, "run end");
        log_->note(*this, LogEvent::RunEnd,
                   stats_.stopReason.empty()
                       ? "run ends (mission window reached)"
                       : "run ends: " + stats_.stopReason);
    }

    if (viewer_ != nullptr) {
        viewerStart = std::chrono::steady_clock::now();
        viewer_->end(*this);
        stats_.viewerSeconds += std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - viewerStart)
                                    .count();
    }

    const auto wallEnd = std::chrono::steady_clock::now();
    // Drawing and waiting are not part of what the engine costs, so they come
    // back out — otherwise a watched run would look thousands of times slower
    // than the same run measured.
    stats_.wallSeconds =
        std::chrono::duration<double>(wallEnd - wallStart).count() -
        stats_.viewerSeconds;

    return stats_;
}

}  // namespace wargame
