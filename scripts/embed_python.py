#!/usr/bin/env python3
"""
embed_python.py - Smart Python stdlib embedding for ralph

This script handles embedding the Python stdlib and default tools into
the ralph binary. It solves the zipcopy accumulation problem by:

1. Preserving a clean copy of the base binary (without zip content)
2. Computing content hashes to detect changes
3. Only re-embedding when necessary (base binary or content changed)
4. Always starting from the clean base to avoid accumulation

Usage:
    # Normal embedding (called by make)
    uv run scripts/embed_python.py

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
TARGET = RALPH_ROOT / "ralph"
TARGET_BASE = BUILD_DIR / "ralph.base"  # Clean binary without zip content
STAMP_FILE = BUILD_DIR / ".embed-python.stamp"
TEMP_ZIP = BUILD_DIR / "python-embed.zip"

PYTHON_STDLIB_DIR = RALPH_ROOT / "python" / "build" / "results" / "py-tmp"
PYTHON_DEFAULTS_DIR = RALPH_ROOT / "src" / "tools" / "python_defaults"


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


def get_current_state() -> dict:
    """Compute the current state of all inputs."""
    return {
        "base_binary": compute_file_hash(TARGET_BASE),
        "stdlib": compute_dir_hash(PYTHON_STDLIB_DIR / "lib"),
        "defaults": compute_dir_hash(PYTHON_DEFAULTS_DIR),
    }


def get_stored_state() -> dict | None:
    """Get the stored state from last successful embed."""
    if STAMP_FILE.exists():
        try:
            return json.loads(STAMP_FILE.read_text())
        except (json.JSONDecodeError, KeyError):
            return None
    return None


def save_state(state: dict):
    """Save the current state after successful embed."""
    STAMP_FILE.write_text(json.dumps(state, indent=2))


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


def save_base():
    """Save a clean copy of the base binary (called right after linking)."""
    if not TARGET.exists():
        print(f"Error: {TARGET} not found", file=sys.stderr)
        sys.exit(1)

    BUILD_DIR.mkdir(exist_ok=True)
    shutil.copy2(TARGET, TARGET_BASE)
    print(f"Saved base binary to {TARGET_BASE}")

    # Also invalidate the stamp since base changed
    if STAMP_FILE.exists():
        STAMP_FILE.unlink()


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
    # We cd to src/tools so the zip contains python_defaults/...
    result = run_shell(
        f"zip -qr {TEMP_ZIP.absolute()} python_defaults/",
        cwd=PYTHON_DEFAULTS_DIR.parent
    )
    if result.returncode != 0:
        print(f"Error adding defaults to zip: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    return TEMP_ZIP


def embed(force: bool = False):
    """Perform the embedding operation."""
    # Validate inputs
    if not (PYTHON_STDLIB_DIR / "lib").exists():
        print(f"Error: Python stdlib not found at {PYTHON_STDLIB_DIR}/lib", file=sys.stderr)
        print("Run 'make python' first to build the Python stdlib.", file=sys.stderr)
        sys.exit(1)

    if not PYTHON_DEFAULTS_DIR.exists():
        print(f"Error: Python defaults not found at {PYTHON_DEFAULTS_DIR}", file=sys.stderr)
        sys.exit(1)

    # Check if we have a base binary
    if not TARGET_BASE.exists():
        if TARGET.exists():
            # First time setup: use existing target as base
            # (This handles the case where make was run before this script existed)
            print("No base binary found, checking existing target...")
            # Try to detect if target already has zip content
            result = run_shell(f"unzip -l {TARGET}")
            if result.returncode == 0 and "lib/python" in result.stdout:
                print("Warning: Existing ralph binary already has embedded content.")
                print("To get a clean base, run 'make clean && make' then 'make embed-python'")
                print("Proceeding anyway (may cause size inflation)...")
                shutil.copy2(TARGET, TARGET_BASE)
            else:
                # Target is clean, save it as base
                print("Target appears clean, saving as base binary...")
                shutil.copy2(TARGET, TARGET_BASE)
        else:
            print(f"Error: No base binary at {TARGET_BASE}", file=sys.stderr)
            print("Run 'make' to build ralph first.", file=sys.stderr)
            sys.exit(1)

    # Check if embedding is needed
    current_state = get_current_state()
    stored_state = get_stored_state()

    needs_embed, reason = needs_embedding(current_state, stored_state)

    if not needs_embed and not force:
        print(f"Python embedding up to date ({reason}), skipping.")
        # Make sure target exists (might have been deleted)
        if not TARGET.exists():
            print("Target missing, restoring from embedded state...")
            needs_embed = True
            reason = "target binary missing"
        else:
            return

    print(f"Embedding Python stdlib and default tools ({reason})...")

    # Always start from the clean base
    print(f"  Copying base binary...")
    shutil.copy2(TARGET_BASE, TARGET)

    # Create the zip
    print(f"  Creating embed zip...")
    zip_path = create_embed_zip()

    # Embed using zipcopy
    print(f"  Running zipcopy...")
    result = run_shell(f"zipcopy {zip_path.absolute()} {TARGET.absolute()}")
    if result.returncode != 0:
        print(f"Error running zipcopy: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    # Clean up temp zip
    zip_path.unlink(missing_ok=True)

    # Update state
    # Re-compute base hash since we just copied it
    current_state["base_binary"] = compute_file_hash(TARGET_BASE)
    save_state(current_state)

    # Report size
    size_bytes = TARGET.stat().st_size
    size_mb = size_bytes / (1024 * 1024)
    print(f"Python embedding complete. Binary size: {size_mb:.1f}M")


def main():
    args = sys.argv[1:]

    if "--save-base" in args:
        save_base()
        return

    force = "--force" in args
    embed(force=force)


if __name__ == "__main__":
    main()
