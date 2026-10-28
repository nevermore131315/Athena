// Plain-data snapshots returned by the collectors.
//
// Deliberately dumb: no I/O, no rendering, just fields — same contract as the
// Python dataclasses this port replaces. The UI layer consumes them and never
// knows where the numbers came from.
#pragma once

#include <optional>
#include <string>
#include <vector>

// Fields cover everything NVTOP shows per device except PCIe RX/TX traffic,
// which needs NVML profiling (pmon) rather than an nvidia-smi query field.
struct GPUSample {
    int index = 0;
    std::string name;
    double utilization_percent = 0.0;
    double memory_used_mib = 0.0;
    double memory_total_mib = 0.0;
    std::optional<int> temperature_celsius;
    std::optional<int> temperature_max_celsius;
    std::optional<double> power_watts;
    std::optional<double> power_limit_watts;
    std::optional<int> fan_percent;
    std::optional<int> clock_gpu_mhz;
    std::optional<int> clock_gpu_max_mhz;
    std::optional<int> clock_mem_mhz;
    std::optional<int> clock_mem_max_mhz;
    std::optional<int> pcie_gen;
    std::optional<int> pcie_width;

    // Used/total as a 0..1 gauge ratio, 0 when the total is unreadable.
    double memory_ratio() const {
        if (memory_total_mib <= 0.0) return 0.0;
        return memory_used_mib / memory_total_mib;
    }
    // Utilization as a 0..1 gauge ratio.
    double util_ratio() const { return utilization_percent / 100.0; }
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
    // Gauge ratios for the NVTOP-style bars.
    double used_ratio() const { return percent_used() / 100.0; }
    double swap_ratio() const {
        if (swap_total_mib <= 0.0) return 0.0;
        return swap_used_mib / swap_total_mib;
    }
};

struct CPUProcessSample {
    int pid = 0;
    std::string name;
    double cpu_percent = 0.0;  // not normalized: >100 = multi-thread busy
    int threads = 0;
};

// Whole-machine CPU usage, the aggregate `cpu ` line of /proc/stat.
struct CpuSystemSample {
    double percent = 0.0;  // 0..100 of ALL cores combined
    int cores = 0;

    // Gauge ratio for the whole-machine bar (percent is already 0..100 of all
    // cores combined).
    double ratio() const { return percent <= 0.0 ? 0.0 : percent / 100.0; }
};

// One complete screen's worth of data, produced atomically by the sampler.
struct Snapshot {
    std::vector<GPUSample> gpus;
    RAMSample ram;
    CpuSystemSample cpu_total;
    std::vector<CPUProcessSample> cpu;
    std::string error;  // non-empty = last collection failed; UI shows it once
};
