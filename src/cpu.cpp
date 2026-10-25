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
        r.cpu_percent =
            static_cast<double>(delta) / (ticks_per_second * elapsed_seconds);
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

}  // namespace cpu
