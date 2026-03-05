"""Extract git patches from modified repositories."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from docker.models.containers import Container


def extract_patch_from_container(container: "Container") -> str:
    """Extract git diff from a Docker container's /testbed/ directory.

    Uses `git -c core.fileMode=false diff` to avoid spurious mode changes
    from the container environment.
    """
    # Stage all changes (including new files) to match local extract_patch
    exit_code, _ = container.exec_run("git add -A", workdir="/testbed/")
    if exit_code != 0:
        return ""
    exit_code, output = container.exec_run(
        "git -c core.fileMode=false diff --cached",
        workdir="/testbed/",
    )
    if exit_code != 0:
        return ""
    return output.decode() if isinstance(output, bytes) else output


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
