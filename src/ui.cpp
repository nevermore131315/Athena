#include "ui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>

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
                 double dt) {
    Snapshot snap;
    snap.gpus = gpu::collect();
    snap.ram = ram::collect();
    snap.cpu = cpu::diff(now_tick, prev_tick, dt, kCpuRows);
    if (snap.gpus.empty() && snap.cpu.empty()) {
        snap.error = "collectors returned nothing (driver unreadable?)";
    }
    return snap;
}

// ---------------------------------------------------------------------------
// Plain-text rendering (--once mode)
// ---------------------------------------------------------------------------

std::string fmt1(double v) { return std::format("{:.1f}", v); }

std::string snapshot_text(const Snapshot& snap) {
    std::string out;
    out += "== GPU ==\n";
    if (snap.gpus.empty()) {
        out += "No NVIDIA GPUs detected. Is nvidia-smi able to reach the driver?\n";
    }
    for (const auto& g : snap.gpus) {
        out += std::format("#{}  {}\n", g.index, g.name);
        out += std::format("util {:>5}%  mem {:.1f}/{:.1f}G\n",
                           fmt1(g.utilization_percent),
                           g.memory_used_mib / 1024.0, g.memory_total_mib / 1024.0);
        out += std::format("{}C  {}\n",
                           g.temperature_celsius ? std::to_string(*g.temperature_celsius)
                                                 : "temp -",
                           g.power_watts ? std::format("{:.0f}W", *g.power_watts)
                                         : "power -");
    }

    out += "\n== RAM ==\n";
    const auto& r = snap.ram;
    out += std::format("used {:>5} / {:.1f}G  {:.0f}%\n", fmt1(r.used_mib / 1024.0),
                       r.total_mib / 1024.0, r.percent_used());
    out += std::format("avail {:>5}G\n", fmt1(r.available_mib / 1024.0));
    out += "Swap\n";
    if (r.swap_total_mib > 0) {
        const double pct = r.swap_used_mib / r.swap_total_mib * 100.0;
        out += std::format("used {:>5} / {:.1f}G  {:.0f}%\n",
                           fmt1(r.swap_used_mib / 1024.0),
                           r.swap_total_mib / 1024.0, pct);
    } else {
        out += "none\n";
    }

    out += "\n== CPU ==\n";
    if (snap.cpu.empty()) {
        out += "No CPU processes\n";
    } else {
        out += std::format("Top {} processes\n", snap.cpu.size());
        for (const auto& p : snap.cpu) {
            std::string name = p.name;
            if (name.size() > 12) name = name.substr(0, 11) + "…";
            out += std::format("{:>7} {:<12}│ {:>6}% {}T\n", p.pid, name,
                               fmt1(p.cpu_percent), p.threads);
        }
    }
    if (!snap.error.empty()) out += "\nwarning: " + snap.error + "\n";
    return out;
}

// ---------------------------------------------------------------------------
// TUI rendering
// ---------------------------------------------------------------------------

std::string trunc_name(const std::string& s) {
    // Byte-wise truncation is fine for ASCII process names; FTXUI re-measures
    // wide glyphs either way.
    if (s.size() <= 12) return s;
    return s.substr(0, 11) + "…";
}

Element gpu_line(const GPUSample& g) {
    auto temp = g.temperature_celsius
                    ? text(std::format("{}C", *g.temperature_celsius))
                    : text("temp -");
    if (g.temperature_celsius && *g.temperature_celsius >= 70)
        temp = bold(temp);
    auto power = g.power_watts ? text(std::format("{:.0f}W", *g.power_watts))
                               : text("power -");

    return vbox({
        hbox({text(std::format("#{}  ", g.index)) | color(Color::Cyan) | bold,
              text(g.name) | bold}),
        hbox({text(std::format("util {:>5}%  ", fmt1(g.utilization_percent))) |
                  color(Color::Yellow),
              text(std::format("mem {:.1f}/{:.1f}G", g.memory_used_mib / 1024.0,
                               g.memory_total_mib / 1024.0))}),
        hbox({temp, text("  "), power}),
    });
}

Element panel(const std::string& title, Color border, Element inner) {
    return color(border, window(text(title), std::move(inner))) | flex;
}

Element render(const Snapshot& snap) {
    Elements gpu_children;
    if (snap.gpus.empty()) {
        gpu_children.push_back(
            text("No NVIDIA GPUs detected.\nIs nvidia-smi able to reach the "
                 "driver?") |
            color(Color::Red) | bold);
    } else {
        for (size_t i = 0; i < snap.gpus.size(); ++i) {
            if (i) gpu_children.push_back(text(""));
            gpu_children.push_back(gpu_line(snap.gpus[i]));
        }
    }

    const auto& r = snap.ram;
    Element pct_element =
        text(std::format("{:.0f}%", r.percent_used()));
    if (r.percent_used() >= 90.0)
        pct_element = color(Color::Red, bold(pct_element));
    else
        pct_element = color(Color::Yellow, pct_element);
    Elements ram_children = {
        text("RAM") | color(Color::Magenta) | bold,
        hbox({text(std::format("used {:>5} / {:.1f}G  ", fmt1(r.used_mib / 1024.0),
                               r.total_mib / 1024.0)),
              pct_element}),
        text(std::format("avail {:>5}G", fmt1(r.available_mib / 1024.0))) | dim,
        text(""),
        text("Swap") | color(Color::Magenta) | bold,
    };
    if (r.swap_total_mib > 0) {
        const double pct = r.swap_used_mib / r.swap_total_mib * 100.0;
        ram_children.push_back(text(std::format(
            "used {:>5} / {:.1f}G  {:>3}%", fmt1(r.swap_used_mib / 1024.0),
            r.swap_total_mib / 1024.0, static_cast<int>(pct))));
    } else {
        ram_children.push_back(text("none") | dim);
    }

    Elements cpu_children = {text("Top CPU processes") | color(Color::Yellow) |
                             bold};
    for (const auto& p : snap.cpu) {
        const bool hot = p.cpu_percent >= 100.0;
        auto pct = text(std::format("│ {:>6}% {:>2}T", fmt1(p.cpu_percent),
                                    p.threads));
        pct = hot ? color(Color::Red, bold(pct)) : color(Color::Yellow, pct);
        cpu_children.push_back(hbox(
            {text(std::format("{:>7}", p.pid)) | bold,
             text(std::format(" {:<12}", trunc_name(p.name))) | dim,
             std::move(pct)}));
    }
    if (snap.cpu.empty()) {
        cpu_children.push_back(text("No CPU processes") | color(Color::Red) | bold);
    }

    Elements root = {
        window(
            text("Athena — multi-GPU / RAM / CPU    [q] quit"),
            hbox({panel("GPU", Color::Cyan, vbox(std::move(gpu_children))),
                  panel("RAM", Color::Magenta, vbox(std::move(ram_children))),
                  panel("CPU", Color::Yellow, vbox(std::move(cpu_children)))})) |
            flex,
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
    std::this_thread::sleep_for(kBaselineInterval);
    const auto end = std::chrono::steady_clock::now();
    const cpu::CpuTick tick = cpu::read_tick();
    const double dt = std::chrono::duration<double>(end - start).count();
    return snapshot_text(collect(prev, tick, dt));
}

int run_tui() {
    auto screen = ScreenInteractive::FullscreenAlternateScreen();

    SnapshotStore store;
    std::atomic<bool> quit{false};

    std::thread sampler([&] {
        cpu::CpuTick prev = cpu::read_tick();
        auto last = std::chrono::steady_clock::now();

        // First real frame after a short baseline, like the Python original —
        // a jiffy delta over ~0 gives 0% everywhere and an empty first screen.
        std::this_thread::sleep_for(kBaselineInterval);

        while (!quit.load()) {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - last).count();
            const cpu::CpuTick tick = cpu::read_tick();

            store.set(collect(prev, tick, dt));
            prev = tick;
            last = now;
            screen.PostEvent(Event::Custom);

            std::this_thread::sleep_for(kRefreshInterval);
        }
    });

    auto renderer = Renderer([&] { return render(store.get()); });
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
