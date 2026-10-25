"""Textual UI for Athena: a real-time, three-panel resource monitor.

Layout is intentionally close to NVTOP: one panel per subsystem (GPU / RAM /
CPU), refreshed once a second. The UI never talks to the driver directly - it
asks :mod:`athena.core` for fresh samples and only renders them.

Widget note (Textual 8.x): RichLog is the right fit here, but its content API
is ``write(renderable)`` - ``log()`` is a debugging helper that never paints.
Each refresh clears a panel and writes one Rich ``Text`` built by hand below.
Text blocks were chosen over Rich tables on purpose: at a three-panel layout
each panel is narrow, and tables squash their columns into ellipses
(``He… 39…``), while stacked text only ever shortens the process name.
"""
from __future__ import annotations

import logging

from rich.text import Text

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.widgets import Footer, Header, RichLog

from ..core import cpu, gpu, ram
from ..core.sample import CPUProcessSample, GPUSample, RAMSample

log = logging.getLogger(__name__)

_REFRESH_SECONDS = 1.0


class AthenaApp(App):
    """All-in-one GPU / RAM / CPU monitor."""

    TITLE = "Athena"
    SUBTITLE = "multi-GPU / RAM / CPU"

    CSS = """
    #boards {
        layout: horizontal;
        padding: 0 1;
        height: 1fr;
    }
    .panel {
        width: 1fr;
        margin: 0 1;
        padding: 0 1;
    }
    #gpu-panel   { border: solid cyan; }
    #ram-panel   { border: solid magenta; }
    #cpu-panel   { border: solid yellow; }
    RichLog {
        height: 1fr;
        min-width: 0;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal(id="boards"):
            with Vertical(id="gpu-panel", classes="panel"):
                yield RichLog(
                    markup=False, wrap=False, min_width=1, id="gpu-log"
                )
            with Vertical(id="ram-panel", classes="panel"):
                yield RichLog(
                    markup=False, wrap=False, min_width=1, id="ram-log"
                )
            with Vertical(id="cpu-panel", classes="panel"):
                yield RichLog(
                    markup=False, wrap=False, min_width=1, id="cpu-log"
                )
        yield Footer()

    def on_mount(self) -> None:
        self._gpu_log = self.query_one("#gpu-log", RichLog)
        self._ram_log = self.query_one("#ram-log", RichLog)
        self._cpu_log = self.query_one("#cpu-log", RichLog)

        self.refresh_samples()  # show something immediately, don't wait a second
        self.set_interval(_REFRESH_SECONDS, self.refresh_samples)

    def refresh_samples(self) -> None:
        try:
            gpu_samples = gpu.collect()
            ram_sample = ram.collect()
            cpu_rows = cpu.top_processes()
        except Exception as exc:  # keep the dashboard alive if a collector errors
            log.error("failed to collect samples: %s", exc)
            return

        # RichLog is append-only, so reset it, then render a fresh Table.
        self._gpu_log.clear()
        self._gpu_log.write(_gpu_text(gpu_samples))
        self._ram_log.clear()
        self._ram_log.write(_ram_text(ram_sample))
        self._cpu_log.clear()
        self._cpu_log.write(_cpu_text(cpu_rows))


def _gpu_text(samples: list[GPUSample]) -> Text:
    """NVTOP-style stacked blocks: one compact 3-line readout per GPU.

    Per-GPU blocks degrade far better in a narrow panel than a wide table:
    if the terminal is cramped only the name can truncate, while the numbers
    always stay fully readable.
    """
    if not samples:
        return Text(
            "No NVIDIA GPUs detected.\nIs nvidia-smi able to reach the driver?",
            style="bold red",
        )

    out = Text()
    for i, g in enumerate(samples):
        if i:
            out.append("\n")
        out.append(f"#{g.index}  ", style="bold cyan")
        out.append(g.name, style="bold")
        out.append("\n")
        out.append(f"util {g.utilization_percent:5.1f}%  ", style="yellow")
        out.append(f"mem {g.memory_used_mb / 1024:.1f}/{g.memory_total_mb / 1024:.1f}G")
        out.append("\n")
        if g.temperature_celsius is not None:
            out.append(
                f"{g.temperature_celsius}C",
                style="bold red" if g.temperature_celsius >= 70 else "",
            )
        else:
            out.append("temp -")
        out.append("  ")
        out.append(
            f"{g.power_watts:.0f}W" if g.power_watts is not None else "power -"
        )
        out.append("\n")
    return out


def _ram_text(sample: RAMSample) -> Text:
    """Memory readout in GiB (the sample stores MiB; one decimal is plenty).

    Same stacked-block style as the GPU panel: it stays legible no matter how
    narrow the panel gets, where a table would squash its own columns.
    """
    gib = 1024  # MiB -> GiB
    out = Text()
    out.append("RAM\n", style="bold magenta")
    out.append(f"used {sample.used_mb / gib:5.1f} / {sample.total_mb / gib:.1f}G  ")
    out.append(
        f"{sample.percent_used:.0f}%",
        style="bold red" if sample.percent_used >= 90 else "yellow",
    )
    out.append(f"\navail {sample.available_mb / gib:5.1f}G\n", style="dim")
    out.append("Swap\n", style="bold magenta")
    if sample.swap_total_mb:
        swap_pct = sample.swap_used_mb / sample.swap_total_mb * 100
        out.append(
            f"used {sample.swap_used_mb / gib:5.1f} / "
            f"{sample.swap_total_mb / gib:.1f}G  {swap_pct:.0f}%"
        )
    else:
        out.append("none", style="dim")
    return out


def _cpu_text(rows: list[CPUProcessSample], limit: int = 12) -> Text:
    """Top processes, one per line: " PID  name▏cpu% threads".

    A 4-column Table at a 3-panel layout gives each column ~5 chars and Rich
    truncates *mid-word* (He…, 39…). Stacked text only ever shortens the
    process name, and the numbers — the part you actually read — survive.
    """
    if not rows:
        return Text("No CPU processes", style="bold red")

    out = Text()
    out.append(f"Top {len(rows)} processes\n", style="bold yellow")
    for r in rows[:limit]:
        name = r.name if len(r.name) <= 12 else r.name[:11] + "…"
        out.append(f"{r.pid:>7}", style="bold")
        out.append(f" {name:<12}", style="dim")
        hot = r.cpu_percent >= 100.0
        out.append(f"│ {r.cpu_percent:6.1f}%", style="bold red" if hot else "yellow")
        out.append(f" {r.threads}T")
        out.append("\n")
    return out
