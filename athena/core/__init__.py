"""Data-collection layer for Athena.

Each module here is a pure collector: it reads hardware state and returns plain
data objects (see :mod:`athena.core.sample`). These know nothing about the UI,
which keeps them testable and reusable.
"""

from . import cpu, gpu, ram  # noqa: F401
from .sample import CPUProcessSample, GPUSample, RAMSample  # noqa: F401

__all__ = ["cpu", "gpu", "ram", "GPUSample", "RAMSample", "CPUProcessSample"]
