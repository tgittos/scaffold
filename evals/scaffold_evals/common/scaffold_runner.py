"""Core scaffold binary invocation and JSON output parsing."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
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
    system_prompt: str,
    scaffold_binary: str = "out/scaffold",
    model: str | None = None,
    home_dir: Path | None = None,
    scaffold_home: Path | None = None,
    timeout: int = 600,
    env_vars: dict[str, str] | None = None,
    debug: bool = False,
) -> ScaffoldResult:
    """Invoke scaffold binary in single-shot JSON mode.

    Writes system prompt to a temp file (scaffold reads and unlinks it),
    runs the binary, and parses JSONL output.

    If scaffold_home is provided, OAuth credentials (oauth2.db) are copied
    from it into home_dir so the instance can reuse an existing login.
    """
    # Write system prompt to temp file
    fd, prompt_path = tempfile.mkstemp(suffix=".txt", prefix="scaffold_prompt_")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(system_prompt)
    except Exception:
        os.close(fd)
        raise

    cmd = [
        "bash", str(scaffold_binary),
        "--json",
        "--yolo",
        "--system-prompt-file", prompt_path,
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
    finally:
        # Clean up temp file if scaffold didn't get to it
        Path(prompt_path).unlink(missing_ok=True)

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
