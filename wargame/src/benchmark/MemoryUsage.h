// The process's peak resident memory, for the benchmark's memory-per-scale
// figures (Step 7 asks for a speed projection at 2,000 objects; a memory
// projection is the natural companion question).
//
// Windows: GetProcessMemoryInfo's PeakWorkingSetSize, which the OS only ever
// raises, never lowers, for the life of the process. Sampling it before and
// after one scale's runs gives the memory THAT scale's battles actually
// touched, net of whatever smaller scales touched before it — by the time we
// sample, a smaller scale's Simulation and PlainBattle are already destroyed
// (measureOnce builds them as locals), so only the high-water mark they left
// behind survives, exactly what "peak" is for.
//
// Not part of a measured Release run in the sense the self-checks are: it is
// one read of an OS counter, not instrumentation of the engine, so it adds no
// meaningful cost to the loop it brackets.

#pragma once

#include <cstdint>

namespace wargame {

// Bytes. 0 if the platform's counter could not be read.
std::uint64_t peakWorkingSetBytes();

}  // namespace wargame
