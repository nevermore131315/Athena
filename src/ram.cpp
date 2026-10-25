#include "ram.hpp"

#include <fstream>
#include <string>
#include <unordered_map>

namespace ram {
namespace {

// /proc/meminfo reports everything in kB.
constexpr double kKibPerMib = 1024.0;

std::unordered_map<std::string, double> read_meminfo() {
    std::unordered_map<std::string, double> out;
    std::ifstream f("/proc/meminfo");
    std::string key;
    double value = 0.0;
    std::string unit;
    while (f >> key >> value) {
        std::getline(f, unit);  // consume the rest of the line (" kB" or " available")
        if (!key.empty() && key.back() == ':') key.pop_back();
        out[key] = value;
    }
    return out;
}

}  // namespace

RAMSample collect() {
    const auto info = read_meminfo();
    const auto get = [&info](const char* key) {
        const auto it = info.find(key);
        return it == info.end() ? 0.0 : it->second / kKibPerMib;
    };

    RAMSample s;
    s.total_mib = get("MemTotal");
    s.available_mib = get("MemAvailable");
    s.used_mib = s.total_mib - s.available_mib;  // psutil's definition of "used"
    s.swap_total_mib = get("SwapTotal");
    s.swap_used_mib = get("SwapTotal") - get("SwapFree");
    return s;
}

}  // namespace ram
