#include "gpu.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace gpu {
namespace {

// nvidia-smi query columns, in the same order parse_line() expects them.
// Field names taken from this driver's `--help-query-gpu`; the nvtop-style
// PCIe RX/TX traffic is omitted because it needs NVML profiling, not a query.
constexpr const char* kQuery =
    "index,name,utilization.gpu,memory.used,memory.total,"
    "temperature.gpu,temperature.gpu_max,fan.speed,"
    "power.draw,power.limit,"
    "clocks.current.graphics,clocks.max.graphics,"
    "clocks.current.memory,clocks.max.memory,"
    "pcie.link.gen.current,pcie.link.width.current";

std::string compact_name(const std::string& name) {
    // Trim the vendor prefix (like NVTOP does) so names fit narrow panels.
    for (const char* prefix : {"NVIDIA GeForce ", "NVIDIA "}) {
        if (name.rfind(prefix, 0) == 0) return name.substr(std::strlen(prefix));
    }
    return name;
}

// Strip surrounding whitespace.
std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(trim(current));
    return fields;
}

bool parse_int(const std::string& s, int& out) {
    // nvidia-smi writes "N/A" for unsupported values, and this driver wraps
    // some of them in brackets ("[N/A]" for temperature.gpu_max).
    if (s.empty() || s.find("N/A") != std::string::npos || s == "-") return false;
    try {
        size_t pos = 0;
        out = std::stoi(s, &pos);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_double(const std::string& s, double& out) {
    if (s.empty() || s.find("N/A") != std::string::npos || s == "-") return false;
    try {
        size_t pos = 0;
        out = std::stod(s, &pos);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

GPUSample parse_line(const std::string& line) {
    const auto f = split_csv(line);
    // Field lookup that tolerates a driver omitting trailing columns.
    const auto field = [&f](size_t i) { return i < f.size() ? f[i] : std::string{}; };

    GPUSample g;
    parse_int(field(0), g.index);
    g.name = (!field(1).empty()) ? compact_name(field(1)) : "Unknown GPU";
    parse_double(field(2), g.utilization_percent);
    parse_double(field(3), g.memory_used_mib);
    parse_double(field(4), g.memory_total_mib);

    int value_i = 0;
    if (parse_int(field(5), value_i)) g.temperature_celsius = value_i;
    if (parse_int(field(6), value_i)) g.temperature_max_celsius = value_i;
    if (parse_int(field(7), value_i)) g.fan_percent = value_i;

    double watts = 0.0;
    if (parse_double(field(8), watts)) g.power_watts = watts;
    if (parse_double(field(9), watts)) g.power_limit_watts = watts;

    if (parse_int(field(10), value_i)) g.clock_gpu_mhz = value_i;
    if (parse_int(field(11), value_i)) g.clock_gpu_max_mhz = value_i;
    if (parse_int(field(12), value_i)) g.clock_mem_mhz = value_i;
    if (parse_int(field(13), value_i)) g.clock_mem_max_mhz = value_i;
    if (parse_int(field(14), value_i)) g.pcie_gen = value_i;
    if (parse_int(field(15), value_i)) g.pcie_width = value_i;
    return g;
}

}  // namespace

std::vector<GPUSample> collect() {
    std::string cmd =
        std::string("nvidia-smi --query-gpu=") + kQuery +
        " --format=csv,noheader,nounits 2>/dev/null";

    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        spdlog::error("nvidia-smi: popen failed: {}", std::strerror(errno));
        return {};
    }

    std::vector<GPUSample> samples;
    std::array<char, 512> buf{};
    std::string output;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe.get())) {
        output.append(buf.data());
    }

    const int rc = pclose(pipe.release());
    if (rc != 0) {
        spdlog::error("nvidia-smi failed (exit {}): is the NVIDIA driver loaded?", rc);
        return {};
    }

    size_t begin = 0;
    while (begin < output.size()) {
        size_t end = output.find('\n', begin);
        if (end == std::string::npos) end = output.size();
        const std::string line = trim(output.substr(begin, end - begin));
        begin = end + 1;
        if (line.empty()) continue;
        try {
            samples.push_back(parse_line(line));
        } catch (const std::exception&) {
            spdlog::warn("skipping unparseable nvidia-smi line: {}", line);
        }
    }
    return samples;
}

}  // namespace gpu
