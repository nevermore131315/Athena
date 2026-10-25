// Plain-data snapshots returned by the collectors.
//
// Deliberately dumb: no I/O, no rendering, just fields — same contract as the
// Python dataclasses this port replaces. The UI layer consumes them and never
// knows where the numbers came from.
#pragma once

#include <optional>
#include <string>
#include <vector>

struct GPUSample {
    int index = 0;
    std::string name;
    double utilization_percent = 0.0;
    double memory_used_mib = 0.0;
    double memory_total_mib = 0.0;
    std::optional<int> temperature_celsius;
    std::optional<double> power_watts;
};

struct RAMSample {
    double total_mib = 0.0;
    double used_mib = 0.0;
    double available_mib = 0.0;
    double swap_total_mib = 0.0;
    double swap_used_mib = 0.0;

    double percent_used() const {
        if (total_mib <= 0.0) return 0.0;
        return used_mib / total_mib * 100.0;
    }
};

struct CPUProcessSample {
    int pid = 0;
    std::string name;
    double cpu_percent = 0.0;  // not normalized: >100 = multi-thread busy
    int threads = 0;
};

// One complete screen's worth of data, produced atomically by the sampler.
struct Snapshot {
    std::vector<GPUSample> gpus;
    RAMSample ram;
    std::vector<CPUProcessSample> cpu;
    std::string error;  // non-empty = last collection failed; UI shows it once
};
