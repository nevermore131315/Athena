// FTXUI front end: a real-time, three-panel resource monitor.
//
// Layout is intentionally close to NVTOP (and the Textual original): one
// bordered panel per subsystem (GPU / RAM / CPU), refreshed once a second.
// The UI never talks to the collectors on its own thread — a sampler thread
// fills the SnapshotStore and posts a redraw event.
#pragma once

#include <string>

namespace ui {

// Collect one complete Snapshot in a single blocking pass (baseline tick,
// short pause, second tick). Used by `athena --once`.
std::string snapshot_text();

// Run the interactive TUI until the user quits (q / ESC / Ctrl-C).
int run_tui();

}  // namespace ui
