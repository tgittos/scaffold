"""Core scaffold binary invocation and JSON output parsing."""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class ScaffoldResult:
    """Result of a scaffold invocation."""

    exit_code: int
    messages: list[dict] = field(default_factory=list)
    raw_stdout: str = ""
    raw_stderr: str = ""


_SYNC_FILES = ("oauth2.db", "config.json")


def _sync_auth(scaffold_home: Path, instance_home: Path) -> None:
    """Copy auth and config state from scaffold_home into instance_home."""
    for name in _SYNC_FILES:
        src = scaffold_home / name
        if src.exists():
            instance_home.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, instance_home / name)


def run_scaffold(
    message: str,
    working_dir: Path,
    scaffold_binary: str = "out/scaffold",
    model: str | None = None,
    home_dir: Path | None = None,
    scaffold_home: Path | None = None,
    timeout: int = 600,
    env_vars: dict[str, str] | None = None,
    debug: bool = False,
) -> ScaffoldResult:
    """Invoke scaffold binary in single-shot JSON mode.

    Uses scaffold's built-in system prompt. The message is passed as
    the user message (typically the issue/feature description).

    If scaffold_home is provided, OAuth credentials (oauth2.db) are copied
    from it into home_dir so the instance can reuse an existing login.
    """
    cmd = [
        "bash", str(scaffold_binary),
        "--json",
        "--yolo",
    ]

    if debug:
        cmd.insert(2, "--debug")

    if model:
        cmd.extend(["--model", model])

    if home_dir:
        home_dir.mkdir(parents=True, exist_ok=True)
        if scaffold_home:
            _sync_auth(scaffold_home, home_dir)
        cmd.extend(["--home", str(home_dir)])

    cmd.append(message)

    try:
        proc = subprocess.run(
            cmd,
            cwd=str(working_dir),
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env_vars,
        )
    except subprocess.TimeoutExpired as e:
        return ScaffoldResult(
            exit_code=-1,
            raw_stdout=e.stdout or "",
            raw_stderr=e.stderr or "",
        )
    except Exception:
        raise

    return _parse_output(proc)


def _parse_output(proc: subprocess.CompletedProcess) -> ScaffoldResult:
    """Parse JSONL output from scaffold into structured result."""
    messages: list[dict] = []

    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
            messages.append(obj)
        except json.JSONDecodeError:
            continue

    return ScaffoldResult(
        exit_code=proc.returncode,
        messages=messages,
        raw_stdout=proc.stdout,
        raw_stderr=proc.stderr,
    )


def extract_last_assistant_text(messages: list[dict]) -> str:
    """Extract all text from the last assistant message.

    Concatenates all text blocks in the last assistant message,
    since a single message may contain multiple text blocks.
    """
    for msg in reversed(messages):
        if msg.get("type") != "assistant":
            continue
        message = msg.get("message", {})
        texts = []
        for block in message.get("content", []):
            if block.get("type") == "text":
                texts.append(block.get("text", ""))
        if texts:
            return "\n".join(texts)
    return ""
