#pragma once
// Keeps background work (A*, scans, ...) off the core the game's own thread is running on.
#if defined(__linux__)
#include <sched.h>
#include <cstdio>
#include <vector>
#endif

namespace baritone::cpu {

// Core the calling thread is on right now, or -1 if unknown. Call it on the game thread
// *before* spawning a worker and hand the result to moveThreadOffCore().
inline int currentCpu() {
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

#if defined(__linux__)
namespace detail {
inline const std::vector<long>& maxFreqs() {  // per-core max kHz, read once
    static const std::vector<long> freqs = [] {
        std::vector<long> f(CPU_SETSIZE, 0);
        for (int i = 0; i < 64; ++i) {
            char path[96];
            std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
            if (FILE* fp = std::fopen(path, "r")) {
                long khz = 0;
                if (std::fscanf(fp, "%ld", &khz) == 1) f[i] = khz;
                std::fclose(fp);
            }
        }
        return f;
    }();
    return freqs;
}
}  // namespace detail
#endif

// Pins the calling thread to the fastest cores other than `avoidCpu`. No-op if that would
// leave no core (single-core device) or the platform can't do it.
inline void moveThreadOffCore(int avoidCpu) {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return;
    const auto& freq = detail::maxFreqs();
    long best = -1;
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (i != avoidCpu && CPU_ISSET(i, &allowed) && freq[i] > best) best = freq[i];
    if (best < 0) return;
    cpu_set_t target;
    CPU_ZERO(&target);
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (i != avoidCpu && CPU_ISSET(i, &allowed) && freq[i] == best) CPU_SET(i, &target);
    sched_setaffinity(0, sizeof target, &target);
#else
    (void)avoidCpu;
#endif
}

}  // namespace baritone::cpu
