#include "cpu.hpp"

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace cpu {
namespace {

// Field indices (0-based) within /proc/<pid>/stat AFTER "pid (comm) ":
// the remainder starts at field 3 (state). utime is field 14, stime 15,
// num_threads 20.
constexpr size_t kUtimeIdx = 11;
constexpr size_t kStimeIdx = 12;
constexpr size_t kThreadsIdx = 17;

// comm (field 2) may contain spaces and parens, so the name runs from the
// FIRST '(' to the LAST ')' and fields resume after it.
bool read_proc_stat(const std::string& path, long& jiffies, int& threads,
                    std::string& name) {
    std::ifstream f(path);
    if (!f) return false;

    std::string rest;
    std::getline(f, rest);
    const size_t open = rest.find('(');
    const size_t close = rest.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open)
        return false;

    name = rest.substr(open + 1, close - open - 1);

    std::istringstream is(rest.substr(close + 2));
    std::string field;
    size_t idx = 0;
    long utime = 0, stime = 0;
    while (is >> field) {
        if (idx == kUtimeIdx) utime = std::stol(field);
        else if (idx == kStimeIdx) stime = std::stol(field);
        else if (idx == kThreadsIdx) threads = std::stoi(field);
        ++idx;
    }
    jiffies = utime + stime;
    return true;
}

}  // namespace

CpuTick read_tick() {
    CpuTick tick;
    DIR* dir = opendir("/proc");
    if (!dir) return tick;

    while (const dirent* de = readdir(dir)) {
        const std::string entry = de->d_name;
        if (entry.empty() || entry[0] < '0' || entry[0] > '9') continue;
        const int pid = std::stoi(entry);
        long jiffies = 0;
        int threads = 0;
        std::string name;
        if (read_proc_stat("/proc/" + entry + "/stat", jiffies, threads, name)) {
            tick.jiffies[pid] = jiffies;
            tick.threads[pid] = threads;
            tick.names[pid] = name.empty() ? ("<pid " + std::to_string(pid) + ">") : name;
        }
    }
    closedir(dir);
    return tick;
}

std::vector<CPUProcessSample> diff(const CpuTick& now, const CpuTick& prev,
                                   double elapsed_seconds, int limit) {
    std::vector<CPUProcessSample> rows;
    if (elapsed_seconds <= 0.0) return rows;

    const double ticks_per_second = static_cast<double>(sysconf(_SC_CLK_TCK));

    for (const auto& [pid, jiffies] : now.jiffies) {
        const auto pj = prev.jiffies.find(pid);
        const long delta = pj == prev.jiffies.end() ? 0 : jiffies - pj->second;
        if (delta < 0) continue;  // jiffy wraparound; skip this tick

        CPUProcessSample r;
        r.pid = pid;
        r.name = now.names.at(pid);
        // delta/(ticks*seconds) is the fraction of a core used (1.0 = one full
        // core); psutil-style percent is 100x that, so >100 = multithreaded.
        r.cpu_percent =
            100.0 * static_cast<double>(delta) / (ticks_per_second * elapsed_seconds);
        if (const auto t = now.threads.find(pid); t != now.threads.end())
            r.threads = t->second;
        rows.push_back(std::move(r));
    }

    std::sort(rows.begin(), rows.end(),
              [](const CPUProcessSample& a, const CPUProcessSample& b) {
                  return a.cpu_percent > b.cpu_percent;
              });
    if (rows.size() > static_cast<size_t>(limit)) rows.resize(limit);
    return rows;
}

SystemTick read_system() {
    SystemTick t;
    std::ifstream f("/proc/stat");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("cpu", 0) != 0) continue;
        std::istringstream is(line);
        std::string label;
        is >> label;
        if (label == "cpu") {
            long value = 0;
            while (is >> value) t.counters.push_back(value);
        } else {
            ++t.cores;  // cpu0, cpu1, … one per hardware thread
        }
    }
    return t;
}

// Whole-machine busy percent (0..100 across every core), the aggregate `cpu `
// line of /proc/stat diffed across the sampling interval. This is what the
// panel's CPU gauge grows against.
CpuSystemSample diff_system(const SystemTick& now, const SystemTick& prev) {
    CpuSystemSample s;
    s.cores = now.cores;
    // Both ticks must hold the same shape of counter list, and some elapsed
    // work, or the gauge reads 0 rather than lying about a fresh CPU.
    if (now.counters.size() < 4 || now.counters.size() != prev.counters.size())
        return s;

    long total_now = 0, idle_now = 0, total_prev = 0, idle_prev = 0;
    for (size_t i = 0; i < now.counters.size(); ++i) {
        // Fields 14/15 are guest and guest_nice, already counted inside `user`
        // — htop skips them and so do we, or the denominator inflates and the
        // gauge under-reads a busy box.
        if (i >= 14) continue;
        total_now += now.counters[i];
        total_prev += prev.counters[i];
        // Field 3 is idle, field 4 (when present) is idle-wait; both mean
        // "not busy".
        if (i == 3 || i == 4) {
            idle_now += now.counters[i];
            idle_prev += prev.counters[i];
        }
    }

    const long elapsed = total_now - total_prev;
    if (elapsed <= 0) return s;
    long busy = elapsed - (idle_now - idle_prev);
    if (busy < 0) busy = 0;
    s.percent = 100.0 * static_cast<double>(busy) / static_cast<double>(elapsed);
    if (s.percent > 100.0) s.percent = 100.0;
    return s;
}

}  // namespace cpu
