#!/usr/bin/env python3
"""Launch an interactive GNM demo using the configured build and an isolated profile."""
from pathlib import Path
import sys

sys.dont_write_bytecode = True

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from run_gnm_demo import main

if __name__ == "__main__":
    raise SystemExit(main(presentation_default=True))
