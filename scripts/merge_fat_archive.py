#!/usr/bin/env python3
"""Merge multiple Cosmopolitan fat archives into a single .a file.

Cosmopolitan stores two separate archives per library:
  path/libfoo.a          — x86_64 objects
  path/.aarch64/libfoo.a — aarch64 objects

cosmoar rcs creates both when given .o files with .aarch64/ concomitants.
cosmoar -M (MRI scripts) isn't supported by cosmoar, but GNU ar handles
the standard ar format fine. We use GNU ar MRI scripts to merge both
architecture archives independently.

Usage: merge_fat_archive.py OUTPUT INPUT_A [INPUT_A ...]
"""

import subprocess
import sys
import tempfile
from pathlib import Path


def merge_archives(output: Path, inputs: list[Path]) -> None:
    """Merge multiple .a files into one using GNU ar MRI script."""
    mri_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".mri", delete=False) as f:
            f.write(f"CREATE {output}\n")
            for lib in inputs:
                f.write(f"ADDLIB {lib}\n")
            f.write("SAVE\nEND\n")
            mri_path = f.name

        with open(mri_path) as mri_file:
            subprocess.check_call(["ar", "-M"], stdin=mri_file)
    finally:
        if mri_path:
            Path(mri_path).unlink(missing_ok=True)


def main() -> None:
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} OUTPUT INPUT_A [INPUT_A ...]", file=sys.stderr)
        sys.exit(1)

    output = Path(sys.argv[1])
    inputs = [Path(a) for a in sys.argv[2:]]

    # Merge x86_64 archives
    merge_archives(output, inputs)

    # Merge aarch64 archives (parallel .aarch64/ tree)
    aarch64_output = output.parent / ".aarch64" / output.name
    aarch64_inputs = [a.parent / ".aarch64" / a.name for a in inputs]

    missing = [str(a) for a in aarch64_inputs if not a.exists()]
    if missing:
        print(f"error: missing aarch64 archives: {', '.join(missing)}", file=sys.stderr)
        sys.exit(1)

    aarch64_output.parent.mkdir(parents=True, exist_ok=True)
    merge_archives(aarch64_output, aarch64_inputs)


if __name__ == "__main__":
    main()
