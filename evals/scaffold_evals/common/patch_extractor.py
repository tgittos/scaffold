"""Extract git patches from modified repositories."""

from __future__ import annotations

import subprocess
from pathlib import Path


def extract_patch(repo_dir: Path) -> str:
    """Stage all changes and return unified diff.

    Runs `git add -A && git diff --cached` to capture all modifications
    (including new/deleted files) as a unified diff string.
    """
    subprocess.run(
        ["git", "add", "-A"],
        cwd=str(repo_dir),
        capture_output=True,
        check=True,
    )

    result = subprocess.run(
        ["git", "diff", "--cached"],
        cwd=str(repo_dir),
        capture_output=True,
        text=True,
        check=True,
    )

    return result.stdout
