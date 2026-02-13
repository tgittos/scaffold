#!/usr/bin/env python3
"""
embed_python.py - Smart Python stdlib embedding

This script handles embedding the Python stdlib and default tools into
a Cosmopolitan binary. It solves the zipcopy accumulation problem by:

1. Preserving a clean copy of the base binary (without zip content)
2. Computing content hashes to detect changes
3. Only re-embedding when necessary (base binary or content changed)
4. Always starting from the clean base to avoid accumulation

Usage:
    # Normal embedding for ralph (called by make)
    uv run scripts/embed_python.py

    # Embed into scaffold
    uv run scripts/embed_python.py --target scaffold

    # Save base binary (called right after linking)
    uv run scripts/embed_python.py --save-base

    # Force re-embed even if hashes match
    uv run scripts/embed_python.py --force
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


# Paths relative to script location
SCRIPT_DIR = Path(__file__).parent.absolute()
RALPH_ROOT = SCRIPT_DIR.parent
BUILD_DIR = RALPH_ROOT / "build"
TEMP_ZIP = BUILD_DIR / "python-embed.zip"

PYTHON_STDLIB_DIR = RALPH_ROOT / "python" / "build" / "results" / "py-tmp"
PYTHON_DEFAULTS_DIR = RALPH_ROOT / "src" / "ralph" / "tools" / "python_defaults"


def get_target_paths(name: str) -> tuple[Path, Path, Path]:
    """Return (target, target_base, stamp_file) for the given binary name."""
    target = RALPH_ROOT / "out" / name
    target_base = BUILD_DIR / f"{name}.base"
    stamp_file = BUILD_DIR / f".embed-python-{name}.stamp"
    return target, target_base, stamp_file


def compute_dir_hash(directory: Path) -> str:
    """Compute a hash of all files in a directory tree.

    Includes both file paths and contents to detect any changes.
    """
    if not directory.exists():
        return "missing"

    h = hashlib.sha256()
    for f in sorted(directory.rglob("*")):
        if f.is_file():
            # Include relative path in hash for structure changes
            rel_path = str(f.relative_to(directory))
            h.update(rel_path.encode())
            h.update(f.read_bytes())
    return h.hexdigest()[:16]  # Truncate for readability


def compute_file_hash(filepath: Path) -> str:
    """Compute hash of a single file."""
    if not filepath.exists():
        return "missing"
    return hashlib.sha256(filepath.read_bytes()).hexdigest()[:16]


def get_current_state(target_base: Path) -> dict:
    """Compute the current state of all inputs."""
    return {
        "base_binary": compute_file_hash(target_base),
        "stdlib": compute_dir_hash(PYTHON_STDLIB_DIR / "lib"),
        "defaults": compute_dir_hash(PYTHON_DEFAULTS_DIR),
    }


def get_stored_state(stamp_file: Path) -> dict | None:
    """Get the stored state from last successful embed."""
    if stamp_file.exists():
        try:
            return json.loads(stamp_file.read_text())
        except (json.JSONDecodeError, KeyError):
            return None
    return None


def save_state(stamp_file: Path, state: dict):
    """Save the current state after successful embed."""
    stamp_file.write_text(json.dumps(state, indent=2))


def needs_embedding(current: dict, stored: dict | None) -> tuple[bool, str]:
    """Check if we need to re-embed and return reason."""
    if stored is None:
        return True, "no previous embed state found"

    if current["base_binary"] == "missing":
        return True, "base binary not found (run --save-base after linking)"

    if current["base_binary"] != stored.get("base_binary"):
        return True, "base binary changed (relinked)"

    if current["stdlib"] != stored.get("stdlib"):
        return True, "Python stdlib changed"

    if current["defaults"] != stored.get("defaults"):
        return True, "Python defaults changed"

    return False, "up to date"


def save_base(target: Path, target_base: Path, stamp_file: Path):
    """Save a clean copy of the base binary (called right after linking)."""
    if not target.exists():
        print(f"Error: {target} not found", file=sys.stderr)
        sys.exit(1)

    BUILD_DIR.mkdir(exist_ok=True)
    shutil.copy2(target, target_base)
    print(f"Saved base binary to {target_base}")

    # Also invalidate the stamp since base changed
    if stamp_file.exists():
        stamp_file.unlink()


def run_shell(cmd: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a shell command, handling APE binaries correctly."""
    return subprocess.run(
        cmd,
        shell=True,
        cwd=cwd,
        capture_output=True,
        text=True
    )


def create_embed_zip() -> Path:
    """Create the zip file with Python stdlib and defaults.

    Uses proper directory structure:
    - lib/python3.12/... for stdlib (matches PYTHONHOME=/zip)
    - python_defaults/... for default tools
    """
    TEMP_ZIP.unlink(missing_ok=True)

    # Add stdlib with lib/ prefix
    # Using -r to recurse, but being careful about working directory
    result = run_shell(
        f"zip -qr {TEMP_ZIP.absolute()} lib/",
        cwd=PYTHON_STDLIB_DIR
    )
    if result.returncode != 0:
        print(f"Error creating stdlib zip: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    # Add defaults with python_defaults/ prefix
    # We cd to src/ralph/tools so the zip contains python_defaults/...
    result = run_shell(
        f"zip -qr {TEMP_ZIP.absolute()} python_defaults/",
        cwd=PYTHON_DEFAULTS_DIR.parent
    )
    if result.returncode != 0:
        print(f"Error adding defaults to zip: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    return TEMP_ZIP


def embed(target: Path, target_base: Path, stamp_file: Path, force: bool = False):
    """Perform the embedding operation."""
    name = target.name

    # Validate inputs
    if not (PYTHON_STDLIB_DIR / "lib").exists():
        print(f"Error: Python stdlib not found at {PYTHON_STDLIB_DIR}/lib", file=sys.stderr)
        print("Run 'make python' first to build the Python stdlib.", file=sys.stderr)
        sys.exit(1)

    if not PYTHON_DEFAULTS_DIR.exists():
        print(f"Error: Python defaults not found at {PYTHON_DEFAULTS_DIR}", file=sys.stderr)
        sys.exit(1)

    # Check if we have a base binary
    if not target_base.exists():
        if target.exists():
            print(f"No base binary found for {name}, checking existing target...")
            result = run_shell(f"unzip -l {target}")
            if result.returncode == 0 and "lib/python" in result.stdout:
                print(f"Warning: Existing {name} binary already has embedded content.")
                print(f"To get a clean base, run 'make clean && make' then 'make embed-python'")
                print("Proceeding anyway (may cause size inflation)...")
                shutil.copy2(target, target_base)
            else:
                print("Target appears clean, saving as base binary...")
                shutil.copy2(target, target_base)
        else:
            print(f"Error: No base binary at {target_base}", file=sys.stderr)
            print(f"Run 'make' to build {name} first.", file=sys.stderr)
            sys.exit(1)

    # Check if embedding is needed
    current_state = get_current_state(target_base)
    stored_state = get_stored_state(stamp_file)

    needs_embed, reason = needs_embedding(current_state, stored_state)

    if not needs_embed and not force:
        print(f"Python embedding for {name} up to date ({reason}), skipping.")
        if not target.exists():
            print("Target missing, restoring from embedded state...")
            needs_embed = True
            reason = "target binary missing"
        else:
            return

    print(f"Embedding Python stdlib and default tools into {name} ({reason})...")

    # Always start from the clean base
    print(f"  Copying base binary...")
    shutil.copy2(target_base, target)

    # Create the zip
    print(f"  Creating embed zip...")
    zip_path = create_embed_zip()

    # Embed using zipcopy
    print(f"  Running zipcopy...")
    result = run_shell(f"zipcopy {zip_path.absolute()} {target.absolute()}")
    if result.returncode != 0:
        print(f"Error running zipcopy: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    # Clean up temp zip
    zip_path.unlink(missing_ok=True)

    # Update state
    current_state["base_binary"] = compute_file_hash(target_base)
    save_state(stamp_file, current_state)

    # Report size
    size_bytes = target.stat().st_size
    size_mb = size_bytes / (1024 * 1024)
    print(f"Python embedding for {name} complete. Binary size: {size_mb:.1f}M")


def main():
    args = sys.argv[1:]

    # Parse --target <name> (default: ralph)
    name = "ralph"
    if "--target" in args:
        idx = args.index("--target")
        if idx + 1 < len(args):
            name = args[idx + 1]
        else:
            print("Error: --target requires a name argument", file=sys.stderr)
            sys.exit(1)

    target, target_base, stamp_file = get_target_paths(name)

    if "--save-base" in args:
        save_base(target, target_base, stamp_file)
        return

    force = "--force" in args
    embed(target, target_base, stamp_file, force=force)


if __name__ == "__main__":
    main()
