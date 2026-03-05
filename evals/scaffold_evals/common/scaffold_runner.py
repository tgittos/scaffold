"""Core scaffold binary invocation and JSON output parsing."""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from shlex import quote as shlex_quote
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from docker.models.containers import Container


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


def _parse_jsonl(text: str) -> list[dict]:
    """Parse JSONL text into a list of dicts, skipping invalid lines."""
    messages: list[dict] = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            messages.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return messages


def _parse_output(proc: subprocess.CompletedProcess) -> ScaffoldResult:
    """Parse JSONL output from scaffold into structured result."""
    return ScaffoldResult(
        exit_code=proc.returncode,
        messages=_parse_jsonl(proc.stdout),
        raw_stdout=proc.stdout,
        raw_stderr=proc.stderr,
    )


def _parse_jsonl_output(output: str, exit_code: int) -> ScaffoldResult:
    """Parse JSONL output from a container exec into structured result."""
    return ScaffoldResult(
        exit_code=exit_code,
        messages=_parse_jsonl(output),
        raw_stdout=output,
        raw_stderr="",
    )


def _write_string_to_container(
    container: "Container", content: str, dst: Path
) -> None:
    """Write a string to a file inside a Docker container via the tar API.

    Unlike swebench's write_to_container (which uses a heredoc command and
    breaks on content with unmatched quotes), this uses put_archive to
    bypass shell parsing entirely.
    """
    import io
    import tarfile

    data = content.encode()
    tar_buf = io.BytesIO()
    with tarfile.open(fileobj=tar_buf, mode="w") as tar:
        info = tarfile.TarInfo(name=dst.name)
        info.size = len(data)
        tar.addfile(info, io.BytesIO(data))
    tar_buf.seek(0)

    container.exec_run(["mkdir", "-p", str(dst.parent)])
    container.put_archive(str(dst.parent), tar_buf.read())


def run_scaffold_in_container(
    container: "Container",
    message: str,
    model: str | None = None,
    env_vars: dict[str, str] | None = None,
    scaffold_home: Path | None = None,
    timeout: int = 600,
    debug: bool = False,
) -> ScaffoldResult:
    """Run scaffold inside a Docker container.

    Writes env vars and auth files into the container, then executes
    scaffold in single-shot JSON mode.
    """
    from swebench.harness.docker_utils import (
        copy_to_container,
        exec_run_with_timeout,
    )

    # Write env vars into container as a sourceable file
    if env_vars:
        env_content = "\n".join(f"export {k}={shlex_quote(v)}" for k, v in env_vars.items())
    else:
        env_content = ""
    _write_string_to_container(container, env_content, Path("/root/.eval_env"))

    # Copy scaffold auth files (oauth2.db, config.json) into container
    if scaffold_home:
        for name in _SYNC_FILES:
            src = scaffold_home / name
            if src.exists():
                copy_to_container(
                    container, src, Path(f"/root/.local/scaffold/{name}")
                )

    # Write the problem statement to a file in the container so we don't
    # have to worry about shell quoting issues with the message content.
    _write_string_to_container(container, message, Path("/tmp/scaffold_message.txt"))

    # Build scaffold command — reads message from file via command substitution
    parts = ["/usr/local/bin/scaffold", "--json", "--yolo"]
    if debug:
        parts.insert(1, "--debug")
    if model:
        parts.extend(["--model", model])
    parts.append('"$(cat /tmp/scaffold_message.txt)"')

    # Append an exit-code sentinel so we can recover it from the output
    # (exec_run_with_timeout does not return the exit code)
    inner = " ".join(parts)
    # Activate the conda testbed env so the agent sees the correct Python
    # (with all project dependencies pre-installed) on PATH.
    # We prepend the conda env's bin dir directly to PATH rather than using
    # `conda activate`, because scaffold inherits PATH at exec time and
    # its shell tool copies os.environ for child processes.
    cmd = [
        "bash", "-c",
        f"source /root/.eval_env && export PATH=/opt/miniconda3/envs/testbed/bin:$PATH; {inner}; echo \"__EXIT_CODE:$?\"",
    ]

    output, timed_out, elapsed = exec_run_with_timeout(container, cmd, timeout)

    if timed_out:
        return ScaffoldResult(
            exit_code=-1,
            raw_stdout=output,
            raw_stderr=f"Timed out after {elapsed:.0f}s",
        )

    # Extract exit code from sentinel line
    exit_code = 0
    clean_output = output
    sentinel = "__EXIT_CODE:"
    last_nl = output.rfind("\n" + sentinel)
    if last_nl != -1:
        tail = output[last_nl + 1 :]
        clean_output = output[:last_nl]
        try:
            exit_code = int(tail[len(sentinel) :].strip())
        except ValueError:
            pass
    elif output.startswith(sentinel):
        # Edge case: sentinel is the only line
        try:
            exit_code = int(output[len(sentinel) :].strip())
        except ValueError:
            pass
        clean_output = ""

    return _parse_jsonl_output(clean_output, exit_code)


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
