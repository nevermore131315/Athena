"""Collect system memory metrics via psutil."""
from __future__ import annotations

import psutil

from .sample import RAMSample

_MB = 1024 ** 2


def collect() -> RAMSample:
    """Return a :class:`RAMSample` snapshot of virtual memory and swap."""
    mem = psutil.virtual_memory()
    swap = psutil.swap_memory()
    return RAMSample(
        total_mb=mem.total // _MB,
        used_mb=mem.used // _MB,
        available_mb=mem.available // _MB,
        swap_total_mb=swap.total // _MB,
        swap_used_mb=swap.used // _MB,
    )
