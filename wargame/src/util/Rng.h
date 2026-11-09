// Random number generator.
//
// std::mt19937_64 produces the same stream on every platform, but the standard
// distributions (uniform_int_distribution and friends) do not: their internals
// are implementation-defined, so the same seed gives different draws under MSVC
// and GCC. The conversions below are written out by hand for that reason — a
// run must be reproducible from its seed on the student's machine and on the
// supervisor's.

#pragma once

#include <cstdint>
#include <random>

namespace wargame {

class Rng {
  public:
    explicit Rng(std::uint64_t seed) : engine_(seed), seed_(seed) {}

    std::uint64_t seed() const { return seed_; }

    // The next raw 64-bit value. Used where only a random ordering key is needed,
    // not a number in a range (the engine's tie-break between simultaneous events).
    std::uint64_t next() { return engine_(); }

    // Uniform in [0, 1).
    double uniform() {
        // 53 significant bits, the most a double holds exactly.
        return static_cast<double>(engine_() >> 11) * (1.0 / 9007199254740992.0);
    }

    // Uniform in [lo, hi).
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

    // Uniform integer in [lo, hi], inclusive. Rejection sampling, so the
    // result is unbiased rather than skewed by a modulo fold.
    std::int64_t uniformInt(std::int64_t lo, std::int64_t hi) {
        if (hi <= lo) {
            return lo;
        }
        const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1u;
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % span);
        std::uint64_t draw = engine_();
        while (draw >= limit) {
            draw = engine_();
        }
        return lo + static_cast<std::int64_t>(draw % span);
    }

    // True with the given probability.
    bool chance(double p) { return uniform() < p; }

  private:
    std::mt19937_64 engine_;
    std::uint64_t seed_;
};

}  // namespace wargame
