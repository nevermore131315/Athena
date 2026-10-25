# Athena

An all-in-one, real-time resource monitor for your machine — a NVTOP-style
dashboard that shows **GPU**, **RAM**, and **CPU** side by side in one window.

Built for multi-GPU boxes: with three NVIDIA cards on this machine, Athena draws
one panel per subsystem and refreshes everything once a second.

## What it shows

| Panel | Source | Displays |
|-------|--------|----------|
| **GPU** | `nvidia-smi` (NVIDIA driver CLI) | Per-GPU utilization %, memory used/total, temperature, power draw |
| **RAM** | `psutil` | RAM used / total / available and %, plus swap usage |
| **CPU** | `psutil` | Top processes ranked by CPU %, with thread counts (a NVTOP-style process list, for the CPU) |

## Install

Athena uses a virtual environment so its dependencies never clash with your
system Python.

```bash
cd Athena
python3 -m venv .venv
source .venv/bin/activate
pip install -e .
```

## Run

```bash
athena
# or, without activating the venv:
python -m athena
```

Use `q` or `Ctrl+C` to quit.

## How it's built (and why)

The code is split into two layers that never talk to each other directly:

```
athena/
├── core/   ← DATA LAYER: pure collectors that return plain data. No screen code.
│   ├── gpu.py   pynvml → GPU snapshots
│   ├── ram.py   psutil → RAM snapshots
│   ├── cpu.py   psutil → CPU process rankings
│   └── sample.py the plain data objects (GPUSample, RAMSample, CPUProcessSample)
├── ui/     ← PRESENTATION: only renders what core/ produces.
│   └── app.py   Textual dashboard
└── main.py ← entry point that ties them together
```

The `core/` layer knows nothing about colors, fonts, or a screen — it just
returns clean data. That means it's easy to test, and if you ever want a web
version or a different layout, the data collection code doesn't need to change.

## Future work

- Per-core CPU gauges (currently process-focused by request)
- GPU process list (which apps are using each card)
- Themes and a config file
- Logging peak values to a file over time
