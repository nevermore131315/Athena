"""Collect CPU metrics via psutil.

Per the project's chosen focus, this module ranks processes by CPU usage - a
NVTOP-style process list aimed at the CPU - rather than drawing per-core gauges.
"""
from __future__ import annotations

import logging
import time

import psutil

from .sample import CPUProcessSample

log = logging.getLogger(__name__)


def top_processes(limit: int = 12) -> list[CPUProcessSample]:
    """Return up to ``limit`` processes ranked by CPU usage (highest first)."""
    # Collect stable attributes once, then sample CPU%. psutil reports
    # cpu_percent relative to the *previous* call and returns 0.0 on the very
    # first call for a process, so we take a baseline pass over every process,
    # pause briefly, then re-sample the same objects for real percentages.
    live = []
    for proc in psutil.process_iter(["pid", "name", "num_threads"]):
        try:
            live.append(
                (proc, proc.info["name"], proc.info["num_threads"])
            )
            proc.cpu_percent()  # baseline: sets the clock, returns 0.0
        except (psutil.AccessDenied, psutil.NoSuchProcess):
            continue

    time.sleep(0.2)

    rows: list[CPUProcessSample] = []
    for proc, name, threads in live:
        try:
            pct = proc.cpu_percent()
            rows.append(
                CPUProcessSample(
                    pid=proc.pid,
                    name=name or f"<pid {proc.pid}>",
                    cpu_percent=round(pct, 1),
                    threads=threads or 0,
                )
            )
        except (psutil.AccessDenied, psutil.NoSuchProcess):
            continue

    rows.sort(key=lambda p: p.cpu_percent, reverse=True)
    return rows[:limit]
