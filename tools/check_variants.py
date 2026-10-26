#!/usr/bin/env python3
"""Checks that the hardware variants of cc1101-sniffer.yaml stay in sync.

A variant may only differ from the root config in the header comment, the
substitutions block and the network block (ethernet or wifi).

Usage: tools/check_variants.py
"""

import difflib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
CONFIG = "cc1101-sniffer.yaml"
VARIANTS = sorted((ROOT / "hardware-config-examples").glob("cc1101-sniffer-*.yaml"))
SKIPPED_BLOCKS = ("substitutions:", "ethernet:", "wifi:")


def shared_part(path: Path) -> list[str]:
    """Lines outside the header comment and the blocks a variant may change."""
    lines = path.read_text(encoding="utf-8").splitlines()
    # The header comment ends at the first top level key.
    start = next(i for i, line in enumerate(lines) if line and not line.startswith("#"))
    result = []
    skipping = False
    for line in lines[start:]:
        top_level = bool(line) and not line[0].isspace() and not line.startswith("#")
        if top_level:
            skipping = line.startswith(SKIPPED_BLOCKS)
        if not skipping:
            result.append(line)
    return result


def main() -> int:
    if not VARIANTS:
        print("FAIL no variants found")
        return 1
    reference = shared_part(ROOT / CONFIG)
    status = 0
    for variant in VARIANTS:
        diff = list(
            difflib.unified_diff(
                reference, shared_part(variant), CONFIG, variant.name, lineterm=""
            )
        )
        if diff:
            print(
                f"FAIL {variant.name} differs from {CONFIG} outside the allowed blocks:"
            )
            print("\n".join(diff))
            status = 1
        else:
            print(f"OK   {variant.name}")
    return status


if __name__ == "__main__":
    sys.exit(main())
