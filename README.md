# Athena

An all-in-one, real-time resource monitor for your machine — a NVTOP-style
dashboard that shows **GPU**, **RAM**, and **CPU** side by side in one window.

Built for multi-GPU boxes: with three NVIDIA cards on this machine, Athena draws
one panel per subsystem and refreshes everything once a second.

Now implemented in **C++ / FTXUI** (ported from the original Python/Textual
prototype, which lives on in git history).

## What it shows

| Panel | Source | Displays |
|-------|--------|----------|
| **GPU** | `nvidia-smi` (NVIDIA driver CLI) | Per-GPU: name, PCIe link (gen @ width), SM/memory clocks, temperature, fan %, power draw / limit, and two growing gauges — activity % and memory used/total |
| **RAM** | `/proc/meminfo` | A gauge for RAM used / total, available bytes, and a gauge for swap used / total |
| **CPU** | `/proc/[pid]/stat` + `/proc/stat` | A gauge for whole-machine busy percent (with the core count), plus the top processes ranked by CPU %, each with a gauge sized against all cores |

Gauges are NVTOP's signature: `[ ▊▊▊░░░░ 21.881Gi/24.000Gi ]` — a track of fixed
cells where one cell is one cell of capacity, and the value rides at the bar's
end. Bar length is therefore honest arithmetic, not decoration: a half-full
memory bar is half-full of VRAM.

FTXUI's own `gauge` element collapses inside a flex-sized panel, so the tracks
are measured against the terminal instead: each panel's minimum footprint
(header prose, labels, bracket furniture) is computed per frame, the leftover
cells become bar length, and the three panels sit side by side only when that
footprint actually fits — narrower terminals stack the panels (nvtop's own
shape) rather than clipping a device name mid-number. `--once` prints the same
gauges as text.

## Build

Dependencies on Fedora: `sudo dnf install ftxui-devel spdlog-devel cmake gcc-c++`

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run

```bash
./build/athena        # interactive TUI (q / Esc / Ctrl+C to quit)
./build/athena --once # print one plain-text snapshot and exit
./build/athena -v     # verbose collector logging to stderr
```

## Install

```bash
sudo cmake --install build   # puts `athena` in /usr/local/bin
```

## How it's built (and why)

The code keeps the same two-layer separation as the Python original, and moves
all sampling onto a background thread so the UI never blocks:

```
src/
├── gpu.cpp/hpp   DATA: nvidia-smi subprocess → GPU snapshots (CSV parse)
├── ram.cpp/hpp   DATA: /proc/meminfo → RAM snapshot (used = total - available)
├── cpu.cpp/hpp   DATA: /proc/[pid]/stat → jiffy deltas → per-process CPU%
├── sample.hpp    the plain data objects (GPUSample, RAMSample, CPUProcessSample)
├── ui.cpp/hpp    PRESENTATION: FTXUI three-panel dashboard + sampler thread
└── main.cpp      entry point (--once / -v / --help)
```

Notes carried over from the Python version:

- `nvidia-smi` CLI over NVML bindings — the driver CLI always works here, and
  one subprocess per second is nothing. Query fields come from this driver's
  `--help-query-gpu`; values it reports as `N/A` or `[N/A]` are treated as
  unknown, not zero.
- CPU% is **not** normalized per core: >100% means more than one core busy.
- The sampler thread takes a short (~250 ms) baseline at startup, then diffs
  cumulative jiffies between refresh ticks — no sleeping in the UI path.

## Future work

- Per-core CPU gauges (the panel currently shows one whole-machine gauge plus
  per-process bars)
- PCIe RX/TX traffic like NVTOP's (needs NVML profiling counters, not an
  `nvidia-smi` query field)
- GPU process list (which apps are using each card)
- Themes and a config file
- Logging peak values to a file over time
