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
constexpr const char* kQuery =
    "index,name,utilization.gpu,memory.used,memory.total,"
    "temperature.gpu,power.draw";

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
    if (s.empty() || s == "N/A" || s == "-") return false;
    try {
        size_t pos = 0;
        out = std::stoi(s, &pos);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_double(const std::string& s, double& out) {
    if (s.empty() || s == "N/A" || s == "-") return false;
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
    GPUSample g;
    parse_int(f.size() > 0 ? f[0] : "", g.index);
    g.name = (f.size() > 1 && !f[1].empty()) ? compact_name(f[1]) : "Unknown GPU";
    parse_double(f.size() > 2 ? f[2] : "", g.utilization_percent);
    parse_double(f.size() > 3 ? f[3] : "", g.memory_used_mib);
    parse_double(f.size() > 4 ? f[4] : "", g.memory_total_mib);

    int temp = 0;
    if (parse_int(f.size() > 5 ? f[5] : "", temp)) g.temperature_celsius = temp;
    double watts = 0.0;
    if (parse_double(f.size() > 6 ? f[6] : "", watts)) g.power_watts = watts;
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
