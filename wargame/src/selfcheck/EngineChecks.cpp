// Checks of the discrete-event engine: ordering, determinism and causality.

#include "config/Config.h"
#include "engine/Simulation.h"
#include "entities/Entity.h"
#include "selfcheck/CheckReport.h"
#include "selfcheck/SelfChecks.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace wargame::selfcheck {

namespace {

// A stand-in entity for exercising the event loop: it wakes itself on a fixed
// period and records the order in which events reached it. It is a test
// fixture, not part of the model — the real units are InfantryBattalion.
class Ticker : public wargame::Entity {
  public:
    // `active` false makes an entity that wakes but never reports activity, to
    // exercise the stalemate stop.
    Ticker(wargame::Tick period, wargame::Side s, bool active = true)
        : period_(period), active_(active) {
        side_ = s;
    }

    const char* kind() const override { return "ticker"; }

    bool onEvent(const wargame::Event& e) override {
        // Every event must arrive at a tick no earlier than the previous one.
        if (e.time < lastTime_) {
            timeWentBackwards = true;
        }
        lastTime_ = e.time;

        switch (e.type) {
            case wargame::EventType::SimStart:
            case wargame::EventType::Wake: {
                ++wakes;
                if (active_) {
                    sim_->noteActivity();
                }
                wargame::Event next(e.time + period_, wargame::EventType::Wake, id_, id_);
                next.addArg(static_cast<std::int32_t>(wakes));
                sim_->postEvent(next);
                return true;
            }
            case wargame::EventType::SimStop:
                ++stops;
                return true;
            default:
                return false;
        }
    }

    std::int64_t wakes = 0;
    std::int64_t stops = 0;
    bool timeWentBackwards = false;

  private:
    wargame::Tick period_ = 1;
    bool active_ = true;
    wargame::Tick lastTime_ = 0;
};

// Wakes once, on the same tick as all its siblings, and records its id. The order
// of the ids is the order in which the engine delivered the simultaneous events.
class Recorder : public wargame::Entity {
  public:
    explicit Recorder(std::vector<std::int32_t>* order) : order_(order) {}

    const char* kind() const override { return "recorder"; }

    bool onEvent(const wargame::Event& e) override {
        if (e.type == wargame::EventType::SimStart) {
            sim_->postEvent(
                wargame::Event(10, wargame::EventType::Wake, id_, id_));
            return true;
        }
        if (e.type == wargame::EventType::Wake) {
            order_->push_back(id_);
            sim_->noteActivity();
            return true;
        }
        return false;
    }

  private:
    std::vector<std::int32_t>* order_;
};

// Delivery order of 50 events that all fall on the same tick.
std::vector<std::int32_t> sameTickOrder(const wargame::Config& cfg,
                                        bool randomMode, std::uint64_t seed) {
    wargame::Simulation sim;
    sim.init(cfg);
    sim.setRandomTieBreak(randomMode);
    sim.reseed(seed);

    std::vector<std::int32_t> order;
    for (int i = 0; i < 50; ++i) {
        sim.addEntity(std::make_unique<Recorder>(&order));
    }
    sim.run();
    return order;
}

bool isAscending(const std::vector<std::int32_t>& v) {
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] < v[i - 1]) {
            return false;
        }
    }
    return true;
}

struct ScenarioResult {
    wargame::RunStats stats;
    std::int64_t totalWakes = 0;
    std::int64_t totalStops = 0;
    bool timeOrderHeld = true;
};

// Builds and runs a fresh engine with `count` tickers. Returning the result
// rather than reusing one engine keeps the two determinism runs independent.
ScenarioResult runScenario(const wargame::Config& cfg, int count, wargame::Tick period) {
    wargame::Simulation sim;
    sim.init(cfg);

    std::vector<Ticker*> tickers;
    tickers.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const wargame::Side s = (i % 2 == 0) ? wargame::Side::Blue : wargame::Side::Red;
        auto t = std::make_unique<Ticker>(period, s);
        Ticker* raw = t.get();
        sim.addEntity(std::move(t));
        tickers.push_back(raw);
    }

    ScenarioResult r;
    r.stats = sim.run();
    for (const Ticker* t : tickers) {
        r.totalWakes += t->wakes;
        r.totalStops += t->stops;
        if (t->timeWentBackwards) {
            r.timeOrderHeld = false;
        }
    }
    return r;
}

}  // namespace

void runEngineChecks(const wargame::Config& cfg) {
    // A short, dense scenario: enough entities and events to exercise the
    // ordering, few enough to stay instant.
    const int count = 200;
    const wargame::Tick period = 1000;

    const ScenarioResult a = runScenario(cfg, count, period);
    const ScenarioResult b = runScenario(cfg, count, period);

    report("events arrive in non-decreasing tick order", a.timeOrderHeld);
    report("every entity received SimStart and SimStop",
           a.totalStops == count && a.totalWakes > count);

    // The point of the sequence number: two identical runs must agree event for
    // event. Without it, simultaneous events would come out in whatever order
    // the heap produced.
    report("two runs with the same seed give the same event count",
           a.stats.eventsProcessed == b.stats.eventsProcessed);
    report("two runs with the same seed give the same checksum",
           a.stats.checksum == b.stats.checksum);
    report("two runs with the same seed give the same entity behaviour",
           a.totalWakes == b.totalWakes);

    // An event in the past is a programming error and must throw rather than
    // silently corrupt causality.
    bool threwOnPastEvent = false;
    try {
        wargame::Simulation sim;
        sim.init(cfg);
        sim.addEntity(std::make_unique<Ticker>(period, wargame::Side::Blue));
        sim.run();  // leaves the clock at the end of the run
        sim.postEvent(wargame::Event(0, wargame::EventType::Wake, 0, 0));
    } catch (const std::logic_error&) {
        threwOnPastEvent = true;
    }
    report("posting an event in the past throws", threwOnPastEvent);

    // Who acts first among events on the same tick. In creation_order mode it is
    // the one created first. In random mode it must NOT be (or the first-created
    // side keeps an edge), yet the same seed must still give the same order.
    const auto inOrder = sameTickOrder(cfg, false, 7);
    const auto randomA = sameTickOrder(cfg, true, 7);
    const auto randomB = sameTickOrder(cfg, true, 7);
    report("creation_order delivers same-tick events in creation order",
           inOrder.size() == 50 && isAscending(inOrder));
    report("random tie-break does not deliver them in creation order",
           randomA.size() == 50 && !isAscending(randomA));
    report("random tie-break is reproducible for the same seed",
           randomA == randomB);

    // A run in which nothing moves or fires must stop on its own, not idle to
    // the end of the mission window.
    if (cfg.req<double>("sim.stalemate_after_hours") > 0.0) {
        wargame::Simulation sim;
        sim.init(cfg);
        for (int i = 0; i < 3; ++i) {
            sim.addEntity(std::make_unique<Ticker>(period, wargame::Side::Blue,
                                                   /*active=*/false));
        }
        const wargame::RunStats s = sim.run();
        const bool early = s.stoppedEarly && s.endTick < sim.endOfRun() / 2;
        const bool named = s.stopReason.rfind("stalemate", 0) == 0;
        report("a run with no activity stops as a stalemate, long before the end",
               early && named);
    } else {
        std::cout << "  skip a run with no activity stops as a stalemate "
                     "(sim.stalemate_after_hours is 0, so the stop is off)\n";
    }

    std::cout << "\nengine run (" << count << " entities waking every " << period
              << " ticks):\n  " << a.stats.str() << "\n";
}

}  // namespace wargame::selfcheck
