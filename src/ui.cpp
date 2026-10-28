#include "ui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cpu.hpp"
#include "gpu.hpp"
#include "ram.hpp"
#include "sample.hpp"

namespace ui {
namespace {

using namespace ftxui;  // NOLINT — dom element names are unwieldy otherwise

constexpr std::chrono::milliseconds kRefreshInterval{1000};
constexpr std::chrono::milliseconds kBaselineInterval{250};
constexpr int kCpuRows = 12;

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

// One collection pass: diff `now_tick` against `prev_tick` over `dt` seconds.
Snapshot collect(const cpu::CpuTick& prev_tick, const cpu::CpuTick& now_tick,
                 const cpu::SystemTick& prev_system,
                 const cpu::SystemTick& now_system, double dt) {
    Snapshot snap;
    snap.gpus = gpu::collect();
    snap.ram = ram::collect();
    snap.cpu_total = cpu::diff_system(now_system, prev_system);
    snap.cpu = cpu::diff(now_tick, prev_tick, dt, kCpuRows);
    if (snap.gpus.empty() && snap.cpu.empty()) {
        snap.error = "collectors returned nothing (driver unreadable?)";
    }
    return snap;
}

// ---------------------------------------------------------------------------
// NVTOP-style value formatting and gauges
// ---------------------------------------------------------------------------

std::string fmt1(double v) { return std::format("{:.1f}", v); }

// nvtop's human sizes: 22.270Gi / 907.0Mi, so a bar's label keeps the same
// shape as the bar itself.
std::string fmt_human(double mib) {
    if (mib < 0.0) mib = 0.0;
    const double gib = mib / 1024.0;
    if (gib >= 1.0) return std::format("{:.3f}Gi", gib);
    return std::format("{:.1f}Mi", mib);
}

// Every gauge is clamped once, here, so a bad collector value can never push a
// bar past its bracket.
double clamp01(double r) {
    if (r < 0.0) return 0.0;
    if (r > 1.0) return 1.0;
    return r;
}

Color gauge_color(double ratio) {
    if (ratio >= 0.9) return Color::Red;
    if (ratio >= 0.7) return Color::Yellow;
    return Color::Cyan;
}

// Cells, not bytes: everything Athena prints is single-cell (° ▊ ░ …), so a
// UTF-8 lead byte counts one cell and a continuation byte counts none.
int cell_count(const std::string& s) {
    int n = 0;
    for (const unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string trunc_cells(const std::string& s, int n) {
    if (n <= 0) return {};
    std::string out;
    int cells = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
            out.push_back(s[i]);  // continuation of a glyph already counted
            continue;
        }
        if (cells >= n) break;
        out.push_back(s[i]);
        ++cells;
    }
    return out;
}

std::string repeat(int n, const std::string& unit) {
    std::string out;
    out.reserve(static_cast<std::size_t>(std::max(0, n)) * unit.size());
    for (int i = 0; i < n; ++i) out += unit;
    return out;
}

// nvtop's gauges are fixed-width tracks: a bar that grows with usage, the
// value riding at the bar's end, inside brackets. FTXUI's own `gauge` element
// collapses inside a flex-sized panel, so the track is built to the width the
// layout hands over — one cell of bar is one cell of capacity, which is what
// makes the length honest rather than merely decorative.
constexpr const char* kBarFill = "▊";
constexpr const char* kBarTrack = "░";

// "[ ▊▊▊░░░░ 21.881Gi/24.000Gi ]" measured to exactly `width` cells.
Element gauge_element(double ratio, const std::string& label, int width) {
    if (width < 6) return text(repeat(std::max(0, width), " "));
    const double r = clamp01(ratio);
    std::string lab = label;
    // brackets + inner spaces (4) + the label + the space before it.
    int room = width - 4 - (lab.empty() ? 0 : cell_count(lab) + 1);
    if (room < 1) {  // the label is eating the track; trim it, never the bar
        lab = trunc_cells(lab, std::max(0, width - 5));
        room = width - 4 - (lab.empty() ? 0 : cell_count(lab) + 1);
    }
    const int filled = static_cast<int>(std::lround(r * room));
    return hbox({
        text("[ ") | dim,
        text(repeat(filled, kBarFill)) | color(gauge_color(r)),
        text(repeat(room - filled, kBarTrack)) | dim,
        lab.empty() ? text("") : text(" " + lab),
        text("]"),
    });
}

// The same track without brackets, for process rows.
Element bare_gauge(double ratio, Color hint, const std::string& label, int width) {
    if (width < 3) return text(repeat(std::max(0, width), " "));
    const double r = clamp01(ratio);
    std::string lab = label;
    int room = width - 1 - (lab.empty() ? 0 : cell_count(lab) + 1);
    if (room < 1) {
        lab = trunc_cells(lab, std::max(0, width - 2));
        room = width - 1 - (lab.empty() ? 0 : cell_count(lab) + 1);
    }
    const int filled = static_cast<int>(std::lround(r * room));
    return hbox({
        text(repeat(filled, kBarFill)) | color(hint),
        text(repeat(room - filled, kBarTrack)) | dim,
        lab.empty() ? text("") : text(" " + lab),
    });
}

// nvtop's "PCIe GEN 1@16x" — link generation and negotiated width.
std::string fmt_pcie(const GPUSample& g) {
    if (!g.pcie_gen || !g.pcie_width) return {};
    return std::format("PCIe GEN {}@{}x", *g.pcie_gen, *g.pcie_width);
}

// nvtop's clock line: what the card is running at now.
std::string fmt_clocks(const GPUSample& g) {
    std::string out;
    if (g.clock_gpu_mhz) out += std::format(" GPU {}MHz", *g.clock_gpu_mhz);
    if (g.clock_mem_mhz) out += std::format(" MEM {}MHz", *g.clock_mem_mhz);
    return out;
}

// nvtop's power line is the draw against its limit, so the number says what
// headroom the card still has.
std::string fmt_power(const GPUSample& g) {
    if (!g.power_watts) return "power -";
    if (g.power_limit_watts)
        return std::format("{:.0f}/{:.0f} W", *g.power_watts, *g.power_limit_watts);
    return std::format("{:.0f} W", *g.power_watts);
}

// ---------------------------------------------------------------------------
// Plain-text rendering (--once mode)
// ---------------------------------------------------------------------------

// A text-mode gauge of a fixed track, so headless output reads like the TUI.
constexpr int kTextBarWidth = 24;

std::string text_bar(double ratio) {
    const double r = clamp01(ratio);
    const int filled = static_cast<int>(std::lround(r * kTextBarWidth));
    return repeat(filled, kBarFill) + repeat(kTextBarWidth - filled, kBarTrack);
}

std::string text_gauge(double ratio, const std::string& label) {
    return "[" + text_bar(ratio) + (label.empty() ? "" : " " + label) + "]";
}

std::string snapshot_text(const Snapshot& snap) {
    std::string out;
    out += "== GPU ==\n";
    if (snap.gpus.empty()) {
        out += "No NVIDIA GPUs detected. Is nvidia-smi able to reach the driver?\n";
    }
    for (const auto& g : snap.gpus) {
        std::string head = std::format("#{:<2}[{}]", g.index, g.name);
        const std::string meta = fmt_pcie(g) + fmt_clocks(g);
        if (!meta.empty()) head += "  " + meta;
        out += head + "\n";
        out += std::format("TEMP {}  FAN {}  POW {}\n",
                           g.temperature_celsius
                               ? std::to_string(*g.temperature_celsius) + "\xC2\xB0" "C"
                               : "-",
                           g.fan_percent ? std::to_string(*g.fan_percent) + "%" : "-",
                           fmt_power(g));
        out += std::format("GPU{} MEM{}\n",
                           text_gauge(g.util_ratio(),
                                      std::format("{:.0f}%", g.utilization_percent)),
                           text_gauge(g.memory_ratio(),
                                      fmt_human(g.memory_used_mib) + "/" +
                                          fmt_human(g.memory_total_mib)));
    }

    out += "\n== RAM ==\n";
    const auto& r = snap.ram;
    out += std::format("RAM {} {:.0f}%\n", text_gauge(r.used_ratio(),
                                                    fmt_human(r.used_mib) + "/" +
                                                        fmt_human(r.total_mib)),
                       r.percent_used());
    out += std::format("avail {}\n", fmt_human(r.available_mib));
    if (r.swap_total_mib > 0) {
        out += std::format("SWP {}\n", text_gauge(r.swap_ratio(),
                                                 fmt_human(r.swap_used_mib) + "/" +
                                                     fmt_human(r.swap_total_mib)));
    } else {
        out += "SWP none\n";
    }

    out += "\n== CPU ==\n";
    out += std::format("CPU {}  {} cores\n", text_gauge(snap.cpu_total.ratio(),
                                                      std::format("{:.0f}%", snap.cpu_total.percent)),
                       snap.cpu_total.cores);
    if (snap.cpu.empty()) {
        out += "No CPU processes\n";
    } else {
        out += std::format("Top {} processes (bar = share of all {} cores)\n",
                           snap.cpu.size(), snap.cpu_total.cores);
        const double capacity =
            snap.cpu_total.cores > 0 ? 100.0 * snap.cpu_total.cores : 100.0;
        for (const auto& p : snap.cpu) {
            std::string name = p.name;
            if (name.size() > 12) name = name.substr(0, 11) + "…";
            out += std::format("{:>7} {:<12} {} {:>3}T\n", p.pid, name,
                               text_gauge(p.cpu_percent / capacity,
                                          fmt1(p.cpu_percent) + "%"),
                               p.threads);
        }
    }
    if (!snap.error.empty()) out += "\nwarning: " + snap.error + "\n";
    return out;
}

// ---------------------------------------------------------------------------
// TUI rendering
// ---------------------------------------------------------------------------

std::string trunc_name(const std::string& s) {
    if (cell_count(s) <= 12) return s;
    return trunc_cells(s, 11) + "…";
}

// nvtop's status line for one device: temperature, then power draw, then fan.
std::string status_line(const GPUSample& g) {
    std::string out = g.temperature_celsius
                          ? std::format("{}\xC2\xB0" "C", *g.temperature_celsius)
                          : "temp -";
    if (g.power_watts) out += " " + fmt_power(g);
    if (g.fan_percent) out += std::format(" FAN {}%", *g.fan_percent);
    return out;
}

// One device, three lines: header, status, then the two gauges sharing the
// panel's width.
Element gpu_block(const GPUSample& g, int width) {
    const std::string name = std::format("#{} [{}]", g.index, g.name);
    const std::string meta = fmt_pcie(g) + fmt_clocks(g);

    // The two gauges split the row. The memory gauge carries a wide label, so
    // it gets the larger share of the track.
    const int row_room = width - 3 - 1 - 3;  // "GPU" + " " + "MEM"
    const int first = row_room / 3;
    const int second = row_room - first;

    auto status = text(status_line(g)) | color(Color::Cyan);
    if (g.temperature_celsius && *g.temperature_celsius >= 70)
        status = color(Color::Red, bold(text(status_line(g))));

    return vbox({
        hbox({text(name) | color(Color::Cyan) | bold,
              meta.empty() ? text("") : text("  " + meta) | dim}),
        hbox({std::move(status)}),
        hbox({text("GPU") | color(Color::Cyan),
              gauge_element(g.util_ratio(),
                            std::format("{:.0f}%", g.utilization_percent), first),
              text(" MEM") | color(Color::Cyan),
              gauge_element(g.memory_ratio(),
                            fmt_human(g.memory_used_mib) + "/" +
                                fmt_human(g.memory_total_mib),
                            second)}),
    });
}

Element panel(const std::string& title, Color border, Element inner) {
    return color(border, window(text(title), std::move(inner)));
}

// The cell footprint a line needs before any bar can be drawn: the tags, the
// bracket furniture, the label, plus a track short enough to still read as a
// gauge. Widths are planned against these so nothing gets clipped mid-number.
constexpr int kMinBarCells = 4;

int gpu_header_cells(const GPUSample& g) {
    return cell_count(std::format("#{} [{}]", g.index, g.name)) + 2 +
           cell_count(fmt_pcie(g) + fmt_clocks(g));
}

int gpu_status_cells(const GPUSample& g) { return cell_count(status_line(g)); }

int gpu_gauge_cells(const GPUSample& g) {
    const std::string util_label = std::format("{:.0f}%", g.utilization_percent);
    const std::string mem_label =
        fmt_human(g.memory_used_mib) + "/" + fmt_human(g.memory_total_mib);
    // "GPU" + " " + "MEM" + the two labelled tracks.
    return 3 + 1 + 3 + (4 + cell_count(util_label) + 1 + kMinBarCells) +
           (4 + cell_count(mem_label) + 1 + kMinBarCells);
}

int ram_row_cells(const RAMSample& r) {
    const std::string used_label = fmt_human(r.used_mib) + "/" + fmt_human(r.total_mib);
    const std::string swap_label =
        fmt_human(r.swap_used_mib) + "/" + fmt_human(r.swap_total_mib);
    return std::max({cell_count("RAM ") + 4 + cell_count(used_label) + 1 + kMinBarCells,
                     cell_count("SWP ") + 4 + cell_count(swap_label) + 1 + kMinBarCells,
                     cell_count("AVAIL ") + cell_count(fmt_human(r.available_mib))});
}

int cpu_row_cells(const Snapshot& snap) {
    const std::string head_label =
        std::format("{:.0f}%  {} cores", snap.cpu_total.percent, snap.cpu_total.cores);
    const int header = cell_count("CPU ") + 4 + cell_count(head_label) + 1 + kMinBarCells;
    if (snap.cpu.empty()) return header;
    // pid + name + track + percent label + thread column.
    const std::string pct = fmt1(snap.cpu.front().cpu_percent) + "%";
    const int row = 7 + 1 + 12 + 1 + (1 + cell_count(pct) + 1 + kMinBarCells) + 1 + 5;
    return std::max(header, row);
}

// Panel widths, in content cells (their border pairs are counted separately).
struct PanelWidth {
    int gpu;
    int ram;
    int cpu;
    bool stacked;
};

PanelWidth plan(int width, int gpu_min, int ram_min, int cpu_min) {
    // Side by side only when the three panels' footprints genuinely fit: nvtop
    // stacks too, and stacking early keeps every bar honest.
    const int sum = gpu_min + ram_min + cpu_min;
    const bool side_by_side = width >= 112 && (width - 8) >= sum;
    if (side_by_side) {
        const int content = width - 8;  // outer border + three panel borders
        const int extra = content - sum;
        PanelWidth w{gpu_min + extra * gpu_min / sum, ram_min + extra * ram_min / sum, 0,
                     false};
        w.cpu = content - w.gpu - w.ram;
        return w;
    }
    const int content = width - 8;  // the lower row carries two panels
    PanelWidth w{width - 4, 0, 0, true};
    if (ram_min + cpu_min <= content) {
        const int extra = content - ram_min - cpu_min;
        w.ram = ram_min + extra * ram_min / (ram_min + cpu_min);
    } else {
        w.ram = content * ram_min / (ram_min + cpu_min);
    }
    w.cpu = content - w.ram;
    return w;
}

// The widest header/status line a GPU panel has to hold, prose included.
int gpu_floor_cells(const Snapshot& snap) {
    int worst = cell_count("No NVIDIA GPUs detected.");
    for (const auto& g : snap.gpus)
        worst = std::max({worst, gpu_header_cells(g), gpu_status_cells(g)});
    return worst;
}

// Where the two labelled tracks stop fitting, even at a stub of a bar.
int gpu_gauge_cells_all(const Snapshot& snap) {
    int worst = 0;
    for (const auto& g : snap.gpus) worst = std::max(worst, gpu_gauge_cells(g));
    return worst;
}

// Lines the panels use above the process list, so the list is sized to the
// terminal instead of being sliced through by a window border.
int cpu_rows_available(int height, int gpu_devices, bool stacked) {
    const int gpu_lines = gpu_devices > 0 ? 3 * gpu_devices + (gpu_devices - 1) : 3;
    // title + outer border + each panel's own border pair.
    const int chrome = 1 + 2 + (stacked ? 4 : 6);
    const int above = stacked ? gpu_lines + 2 + 1 : 0;  // GPU panel + row gap
    const int cpu_header = 2;
    return std::max(1, height - chrome - above - cpu_header - (stacked ? 0 : 6));
}

Element render(const Snapshot& snap, int width, int height) {
    const int gpu_min = std::max(gpu_floor_cells(snap), gpu_gauge_cells_all(snap));
    const PanelWidth pw = plan(width, gpu_min, ram_row_cells(snap.ram),
                               cpu_row_cells(snap));
    const int rows = cpu_rows_available(height, static_cast<int>(snap.gpus.size()), pw.stacked);

    Elements gpu_children;
    if (snap.gpus.empty()) {
        gpu_children.push_back(
            text("No NVIDIA GPUs detected.\nIs nvidia-smi able to reach the "
                 "driver?") |
            color(Color::Red) | bold);
    } else {
        for (size_t i = 0; i < snap.gpus.size(); ++i) {
            if (i) gpu_children.push_back(text(""));
            gpu_children.push_back(gpu_block(snap.gpus[i], pw.gpu));
        }
    }

    const auto& r = snap.ram;
    Elements ram_children = {
        hbox({text("RAM ") | color(Color::Magenta) | bold,
              gauge_element(r.used_ratio(),
                            fmt_human(r.used_mib) + "/" + fmt_human(r.total_mib),
                            pw.ram - 4)}),
        hbox({text("AVAIL ") | color(Color::Magenta) | dim,
              text(fmt_human(r.available_mib)) | dim}),
        text(""),
    };
    if (r.swap_total_mib > 0) {
        ram_children.push_back(hbox({text("SWP ") | color(Color::Magenta) | bold,
                                     gauge_element(r.swap_ratio(),
                                                   fmt_human(r.swap_used_mib) + "/" +
                                                       fmt_human(r.swap_total_mib),
                                                   pw.ram - 4)}));
    } else {
        ram_children.push_back(text("SWP none") | dim);
    }

    Elements cpu_children = {
        hbox({text("CPU ") | color(Color::Yellow) | bold,
              gauge_element(snap.cpu_total.ratio(),
                            std::format("{:.0f}%  {} cores", snap.cpu_total.percent,
                                        snap.cpu_total.cores),
                            pw.cpu - 4)}),
        text(""),
    };
    // A process bar is its share of the whole machine, so the bars and the
    // header gauge read on the same scale.
    const double capacity =
        snap.cpu_total.cores > 0 ? 100.0 * snap.cpu_total.cores : 100.0;
    const int shown = std::min(static_cast<int>(snap.cpu.size()), rows);
    for (int i = 0; i < shown; ++i) {
        const auto& p = snap.cpu[i];
        const bool hot = p.cpu_percent >= 100.0;
        // pid(7) + space + name(12) + space, the bar, then threads in their
        // own column so the label never competes with the track for cells.
        const int bar_w = pw.cpu - 7 - 1 - 12 - 1 - 1 - 5;
        cpu_children.push_back(hbox(
            {text(std::format("{:>7}", p.pid)) | bold,
             text(std::format(" {:<12}", trunc_name(p.name))) | dim,
             text(" ") | dim,
             bare_gauge(p.cpu_percent / capacity, hot ? Color::Red : Color::Yellow,
                        fmt1(p.cpu_percent) + "%", bar_w),
             text(" ") | dim,
             text(std::format("{:>4}T", p.threads)) | dim}));
    }
    if (snap.cpu.empty()) {
        cpu_children.push_back(text("No CPU processes") | color(Color::Red) | bold);
    }

    Element panels = pw.stacked
                         ? vbox({panel("GPU", Color::Cyan, vbox(std::move(gpu_children))),
                                 hbox({panel("RAM", Color::Magenta, vbox(std::move(ram_children))),
                                       panel("CPU", Color::Yellow, vbox(std::move(cpu_children)))})})
                         : hbox({panel("GPU", Color::Cyan, vbox(std::move(gpu_children))),
                                 panel("RAM", Color::Magenta, vbox(std::move(ram_children))),
                                 panel("CPU", Color::Yellow, vbox(std::move(cpu_children)))});

    Elements root = {
        window(text("Athena — multi-GPU / RAM / CPU    [q] quit"), std::move(panels)),
    };
    if (!snap.error.empty()) {
        root.push_back(text("warning: " + snap.error) | color(Color::Red));
    }
    return vbox(std::move(root));
}

// ---------------------------------------------------------------------------
// Snapshot store shared between the sampler thread and the renderer
// ---------------------------------------------------------------------------

class SnapshotStore {
  public:
    void set(Snapshot snap) {
        {
            std::lock_guard lock(mutex_);
            snapshot_ = std::move(snap);
        }
        cv_.notify_all();
    }

    Snapshot get() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void wait_once() const {
        std::unique_lock lock(mutex_);
        cv_.wait(lock);
    }

  private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    Snapshot snapshot_;
};

}  // namespace

std::string snapshot_text() {
    const auto start = std::chrono::steady_clock::now();
    const cpu::CpuTick prev = cpu::read_tick();
    const cpu::SystemTick prev_sys = cpu::read_system();
    std::this_thread::sleep_for(kBaselineInterval);
    const auto end = std::chrono::steady_clock::now();
    const cpu::CpuTick tick = cpu::read_tick();
    const cpu::SystemTick now_sys = cpu::read_system();
    const double dt = std::chrono::duration<double>(end - start).count();
    return snapshot_text(collect(prev, tick, prev_sys, now_sys, dt));
}

int run_tui() {
    auto screen = ScreenInteractive::FullscreenAlternateScreen();

    SnapshotStore store;
    std::atomic<bool> quit{false};

    std::thread sampler([&] {
        cpu::CpuTick prev = cpu::read_tick();
        cpu::SystemTick prev_sys = cpu::read_system();
        auto last = std::chrono::steady_clock::now();

        // First real frame after a short baseline, like the Python original —
        // a jiffy delta over ~0 gives 0% everywhere and an empty first screen.
        std::this_thread::sleep_for(kBaselineInterval);

        while (!quit.load()) {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - last).count();
            const cpu::CpuTick tick = cpu::read_tick();
            const cpu::SystemTick now_sys = cpu::read_system();

            store.set(collect(prev, tick, prev_sys, now_sys, dt));
            prev = tick;
            prev_sys = now_sys;
            last = now;
            screen.PostEvent(Event::Custom);

            std::this_thread::sleep_for(kRefreshInterval);
        }
    });

    // The renderer measures the terminal each frame so the bars and the process
    // list are sized to it, rather than clipped by it.
    auto renderer = Renderer([&] {
        const auto dims = Terminal::Size();
        return render(store.get(), dims.dimx, dims.dimy);
    });
    auto final_component = CatchEvent(renderer, [&](const Event& event) {
        if (event == Event::Character('q') || event == Event::Escape) {
            quit.store(true);
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(final_component);
    quit.store(true);
    sampler.join();
    return 0;
}

}  // namespace ui
