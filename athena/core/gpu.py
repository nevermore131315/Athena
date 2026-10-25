"""Collect GPU metrics from every NVIDIA GPU via ``nvidia-smi``.

We shell out to the ``nvidia-smi`` CLI (part of the NVIDIA driver) and parse its
CSV output rather than using the pynvml Python binding. That binding is currently
unreliable in this environment (the 13.x release cannot initialize NVML), whereas
``nvidia-smi`` ships with the driver and is exactly what tools like NVTOP use.

This module returns plain :class:`GPUSample` objects and knows nothing about
screens or colors.
"""
from __future__ import annotations

import logging
import subprocess

from .sample import GPUSample

log = logging.getLogger(__name__)

# nvidia-smi query columns, in the same order _parse_line() expects them.
_QUERY = (
    "index,name,utilization.gpu,memory.used,memory.total,"
    "temperature.gpu,power.draw"
)


def collect() -> list[GPUSample]:
    """Return a :class:`GPUSample` for every NVIDIA GPU on the machine."""
    samples = []
    for line in _run_nvidia_smi():
        try:
            samples.append(_parse_line(line))
        except ValueError:
            log.warning("skipping unparseable nvidia-smi line: %r", line)
    return samples


def _run_nvidia_smi() -> list[str]:
    """Run nvidia-smi and return its non-empty output lines."""
    try:
        result = subprocess.run(
            [
                "nvidia-smi",
                f"--query-gpu={_QUERY}",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            check=True,
        )
    except FileNotFoundError:
        log.error("nvidia-smi not found on PATH (install the NVIDIA driver)")
        return []
    except subprocess.CalledProcessError as exc:
        log.error("nvidia-smi failed: %s", exc.stderr.strip() or exc)
        return []

    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def _parse_line(line: str) -> GPUSample:
    # Fields are separated by ", " (comma + space); stripping keeps names intact.
    f = [part.strip() for part in line.split(",")]
    return GPUSample(
        index=_int(f, 0),
        name=_compact_name(f[1]) if len(f) > 1 and f[1] else "Unknown GPU",
        utilization_percent=_float(f, 2),
        memory_used_mb=_int(f, 3),
        memory_total_mb=_int(f, 4),
        temperature_celsius=_int(f, 5),
        power_watts=_float(f, 6),
    )


def _compact_name(name: str) -> str:
    """Trim the vendor prefix (like NVTOP does) so names fit narrow panels."""
    for prefix in ("NVIDIA GeForce ", "NVIDIA "):
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


def _int(f: list[str], i: int) -> int:
    try:
        return int(f[i]) if f[i] not in ("", "-") else 0
    except (IndexError, ValueError):
        return 0


def _float(f: list[str], i: int) -> float:
    try:
        return float(f[i]) if f[i] not in ("", "-") else 0.0
    except (IndexError, ValueError):
        return 0.0
