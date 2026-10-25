// Per-process CPU metrics read from /proc.
//
// A NVTOP-style process list aimed at the CPU, matching the Python original:
// ranked by CPU usage (highest first), not per-core gauges.
//
// Sampling model: instead of psutil's two-pass trick (enumerate everything,
// sleep 200ms, enumerate again — all on the UI thread), the caller keeps the
// previous tick's cumulative jiffy counts in a CpuBook and each update computes
// a delta over the real elapsed interval. No blocking, no double iteration.
#pragma once

#include "sample.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cpu {

// One read of /proc: cumulative jiffies and thread counts per pid.
struct CpuTick {
    std::unordered_map<int, long> jiffies;
    std::unordered_map<int, int> threads;
    std::unordered_map<int, std::string> names;
};

// Take one raw tick; cheap enough to call once per refresh.
CpuTick read_tick();

// Compute per-process CPU% by diffing `now` against `prev` (jiffies/second).
// Processes not present in `prev` report 0.0% on their first tick.
std::vector<CPUProcessSample> diff(const CpuTick& now, const CpuTick& prev,
                                   double elapsed_seconds, int limit);

}  // namespace cpu
