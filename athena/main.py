"""Entry point for Athena: `python -m athena` or the `athena` console command."""
from __future__ import annotations

import logging

from .ui.app import AthenaApp


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )
    AthenaApp().run()


if __name__ == "__main__":
    main()
