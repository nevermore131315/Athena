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
| **GPU** | `nvidia-smi` (NVIDIA driver CLI) | Per-GPU utilization %, memory used/total, temperature, power draw |
| **RAM** | `/proc/meminfo` | RAM used / total / available and %, plus swap usage |
| **CPU** | `/proc/[pid]/stat` | Top processes ranked by CPU %, with thread counts (a NVTOP-style process list, for the CPU) |

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
  one subprocess per second is nothing.
- CPU% is **not** normalized per core: >100% means more than one core busy.
- The sampler thread takes a short (~250 ms) baseline at startup, then diffs
  cumulative jiffies between refresh ticks — no sleeping in the UI path.

## Future work

- Per-core CPU gauges (currently process-focused by request)
- GPU process list (which apps are using each card)
- Themes and a config file
- Logging peak values to a file over time
