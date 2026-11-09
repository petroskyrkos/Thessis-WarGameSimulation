#include "benchmark/MemoryUsage.h"

#if defined(_WIN32)
// PSAPI_VERSION 2 makes GetProcessMemoryInfo resolve to K32GetProcessMemoryInfo,
// exported by kernel32.dll since Vista — no separate Psapi.lib to link.
#define PSAPI_VERSION 2
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace wargame {

std::uint64_t peakWorkingSetBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    }
    return 0;
#else
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    // macOS reports ru_maxrss in bytes.
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    // Linux reports it in kilobytes.
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
#endif
}

}  // namespace wargame
