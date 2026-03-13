"""Extract git patches from modified repositories."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from docker.models.containers import Container


def extract_patch_from_container(
    container: "Container", base_commit: str | None = None,
) -> str:
    """Extract git diff from a Docker container's /testbed/ directory.

    Uses `git -c core.fileMode=false diff` to avoid spurious mode changes
    from the container environment.  Excludes binary/artifact files that
    would prevent the patch from being applied during scoring.

    If base_commit is provided, diffs against that commit (captures changes
    even if they were committed during the run, e.g. by test suites).
    Otherwise falls back to diffing staged changes against HEAD.
    """
    # Remove build/test artifacts before staging so they don't pollute the
    # patch.  We do this with rm -rf rather than relying on git pathspec
    # exclusions (`:!pattern`) which older git versions don't support.
    container.exec_run(
        "bash -c '"
        "find . \\( -name \"*.pyc\" -o -name \"__pycache__\" -o -name \".pytest_cache\" "
        "-o -name \"*.pyo\" -o -name \"*.so\" -o -name \"*.egg-info\" "
        "-o -name \".aider.*cache*\" \\) -exec rm -rf {} + 2>/dev/null; true'",
        workdir="/testbed/",
    )
    # Stage all changes (including new files)
    exit_code, _ = container.exec_run("git add -A", workdir="/testbed/")
    if exit_code != 0:
        print("[patch-extract] git add -A failed", flush=True)
        return ""

    if base_commit:
        exit_code, output = container.exec_run(
            f"git -c core.fileMode=false diff {base_commit}",
            workdir="/testbed/",
        )
    else:
        exit_code, output = container.exec_run(
            "git -c core.fileMode=false diff --cached",
            workdir="/testbed/",
        )

    if exit_code != 0:
        err_text = output.decode("utf-8", errors="replace") if isinstance(output, bytes) else output
        print(f"[patch-extract] git diff failed (exit={exit_code}): {err_text[:1000]}", flush=True)
        return ""

    result = output.decode() if isinstance(output, bytes) else output
    if not result.strip():
        # Debug: show what state git is actually in
        _, status = container.exec_run("git status --short", workdir="/testbed/")
        _, log = container.exec_run("git log --oneline -5", workdir="/testbed/")
        status_str = status.decode() if isinstance(status, bytes) else status
        log_str = log.decode() if isinstance(log, bytes) else log
        print(f"[patch-extract] empty diff. git status:\n{status_str}", flush=True)
        print(f"[patch-extract] git log:\n{log_str}", flush=True)
    return result


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
