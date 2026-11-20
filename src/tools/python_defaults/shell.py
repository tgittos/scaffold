"""Execute shell commands.

Thread-safe: true
Match: command

Security Note: This tool intentionally executes arbitrary shell commands.
The security boundary is the user's decision to run ralph and grant it
shell access - not this tool's input validation. The dangerous_patterns
list provides defense-in-depth against obvious accidents, not a security
boundary. This is by design for an agent development tool.
"""


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def shell(command: str, working_dir: str = None, timeout: int = 30,
          capture_stderr: bool = True) -> dict:
    """Execute a shell command and return stdout/stderr. Use for builds, tests, git operations, and system tasks. Prefer dedicated tools (read_file, search_files, apply_patch) over shell equivalents. Avoid interactive commands (vim, less) — they will hang. Calls in the same batch run concurrently; put steps that depend on each other in separate turns.

    Args:
        command: Shell command to execute
        working_dir: Working directory for command execution (optional)
        timeout: Maximum execution time in seconds (default: 30, capped by the shell_max_timeout config, default 300)
        capture_stderr: Whether to capture stderr separately (default: True)

    Returns:
        Dictionary with stdout, stderr, exit_code, execution_time, timed_out, and cancelled
    """
    import os
    import signal
    import subprocess
    import threading
    import time
    from pathlib import Path
    import _ralph_sys

    max_timeout = _ralph_sys.get_config_int("shell_max_timeout", 300)
    if timeout <= 0:
        timeout = 30
    elif timeout > max_timeout:
        timeout = max_timeout

    # Security checks - reject dangerous patterns
    dangerous_patterns = [
        'rm -rf /',
        'rm -rf /*',
        'mkfs',
        'dd if=',
        ':(){ :|:& };:',
        'chmod -R 777 /',
    ]

    cmd_lower = command.lower()
    for pattern in dangerous_patterns:
        if pattern.lower() in cmd_lower:
            return {
                "stdout": "",
                "stderr": "Error: Command failed security validation",
                "exit_code": -1,
                "execution_time": 0.0,
                "timed_out": False,
                "cancelled": False
            }

    # Validate working directory if specified
    if working_dir:
        # Security check - prevent directory traversal
        if _is_traversal_path(working_dir):
            return {
                "stdout": "",
                "stderr": "Error: Invalid working directory path (directory traversal not allowed)",
                "exit_code": -1,
                "execution_time": 0.0,
                "timed_out": False,
                "cancelled": False
            }

        wd = Path(working_dir).resolve()
        if not wd.exists():
            return {
                "stdout": "",
                "stderr": f"Error: Working directory not found: {working_dir}",
                "exit_code": -1,
                "execution_time": 0.0,
                "timed_out": False,
                "cancelled": False
            }
        if not wd.is_dir():
            return {
                "stdout": "",
                "stderr": f"Error: Not a directory: {working_dir}",
                "exit_code": -1,
                "execution_time": 0.0,
                "timed_out": False,
                "cancelled": False
            }
        working_dir = str(wd)

    start = time.time()
    timed_out = False
    cancelled = False

    # Build a clean environment for child processes.
    # Scaffold's embedded Python sets PYTHONHOME=/zip which breaks any
    # system Python invoked by the agent (e.g. `python -m pytest`).
    clean_env = {k: v for k, v in os.environ.items()
                 if k not in ("PYTHONHOME", "PYTHONDONTWRITEBYTECODE")}

    # Report each output line to the spinner's live detail as it arrives.
    # Convert happens in C; guard so shell.py still works if unavailable.
    _progress = getattr(_ralph_sys, "progress", None)
    _cancelled = getattr(_ralph_sys, "cancelled", None)

    def _drain(stream, chunks):
        """Read a text stream line by line, buffering and reporting each line.

        stdout and stderr each get their own thread so neither pipe can fill
        and deadlock the child while we read the other.
        """
        try:
            for line in iter(stream.readline, ''):
                chunks.append(line)
                if _progress is not None:
                    _progress(line.rstrip('\n'))
        finally:
            stream.close()

    def _kill_tree(proc):
        """SIGKILL the command's whole process group so piped children die too."""
        if os.name == "posix":
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                proc.kill()
        else:
            proc.kill()
        proc.wait()

    try:
        proc = subprocess.Popen(
            command,
            shell=True,
            cwd=working_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=clean_env,
            start_new_session=(os.name == "posix"),
        )

        stdout_chunks = []
        stderr_chunks = []
        t_out = threading.Thread(target=_drain, args=(proc.stdout, stdout_chunks))
        t_err = threading.Thread(target=_drain, args=(proc.stderr, stderr_chunks))
        t_out.start()
        t_err.start()

        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                timed_out = True
                _kill_tree(proc)
                break
            try:
                proc.wait(timeout=min(0.1, remaining))
                break
            except subprocess.TimeoutExpired:
                if _cancelled is not None and _cancelled():
                    cancelled = True
                    _kill_tree(proc)
                    break

        # Join the readers so all buffered output is captured, including the
        # final chunk flushed when the pipes close. A background process that
        # left its own group can hold a pipe open; don't wait on it forever.
        t_out.join(timeout=2)
        t_err.join(timeout=2)

        stdout = ''.join(stdout_chunks)
        stderr = ''.join(stderr_chunks) if capture_stderr else ""
        exit_code = -1 if timed_out or cancelled else proc.returncode

    except Exception as e:
        stdout = ""
        stderr = str(e)
        exit_code = -1

    execution_time = time.time() - start

    # Truncate very long output
    max_output = 1024 * 1024  # 1MB
    if len(stdout) > max_output:
        stdout = stdout[:max_output] + '\n[Output truncated at 1MB]'
    if len(stderr) > max_output:
        stderr = stderr[:max_output] + '\n[Output truncated at 1MB]'

    # Hint when commands fail due to missing dependencies
    if exit_code != 0:
        combined = stdout + stderr
        if 'ModuleNotFoundError' in combined or 'No module named' in combined:
            stderr += "\n[Hint: A Python module is missing. Install it (e.g. 'pip install <name>' or 'pip install -e .') then retry.]"
        elif 'Cannot find module' in combined and 'node' in command.lower():
            stderr += "\n[Hint: A Node.js module is missing. Install it (e.g. 'npm install' or 'npm install <name>') then retry.]"
        elif 'command not found' in combined:
            stderr += "\n[Hint: Command not found. Install it (e.g. 'apt-get install -y <name>', 'pip install <name>', or 'npm install -g <name>') then retry.]"

    return {
        "stdout": stdout,
        "stderr": stderr,
        "exit_code": exit_code,
        "execution_time": round(execution_time, 3),
        "timed_out": timed_out,
        "cancelled": cancelled
    }
