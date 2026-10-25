// Entry point for Athena: the `athena` binary.
//
//   athena          run the interactive TUI
//   athena --once   print one plain-text snapshot and exit (headless / scripts)
//   athena -v       log collector warnings to stderr
#include <spdlog/spdlog.h>

#include <cstring>
#include <iostream>

#include "ui.hpp"

int main(int argc, char** argv) {
    bool once = false;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--once") == 0) once = true;
        else if (std::strcmp(argv[i], "-v") == 0) verbose = true;
        else if (std::strcmp(argv[i], "--help") == 0 ||
                 std::strcmp(argv[i], "-h") == 0) {
            std::cout << "usage: athena [--once] [-v]\n";
            return 0;
        }
    }

    spdlog::set_level(verbose ? spdlog::level::info : spdlog::level::err);

    if (once) {
        std::cout << ui::snapshot_text();
        return 0;
    }
    return ui::run_tui();
}
