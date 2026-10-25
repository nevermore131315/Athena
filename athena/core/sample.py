"""Plain-data objects returned by the collectors in :mod:`athena.core`.

These are deliberately dumb: no I/O, no rendering, just fields. The UI layer
consumes them and the tests assert on them. Keeping them separate from psutil
and pynvml means the rest of the app never has to know a raw driver handle exists.
"""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class GPUSample:
    """One snapshot of a single GPU."""

    index: int
    name: str
    utilization_percent: float
    memory_used_mb: int
    memory_total_mb: int
    temperature_celsius: int | None = None
    power_watts: float | None = None
    processes: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class RAMSample:
    """One snapshot of system memory (RAM + swap), all values in MiB."""

    total_mb: int
    used_mb: int
    available_mb: int
    swap_total_mb: int
    swap_used_mb: int

    @property
    def percent_used(self) -> float:
        if self.total_mb == 0:
            return 0.0
        return round(self.used_mb / self.total_mb * 100, 1)


@dataclass(frozen=True)
class CPUProcessSample:
    """One row of the CPU process table."""

    pid: int
    name: str
    cpu_percent: float
    threads: int
