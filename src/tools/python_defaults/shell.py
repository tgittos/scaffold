"""Execute shell commands.

Security Note: This tool intentionally executes arbitrary shell commands.
The security boundary is the user's decision to run ralph and grant it
shell access - not this tool's input validation. The dangerous_patterns
list provides defense-in-depth against obvious accidents, not a security
boundary. This is by design for an agent development tool.
"""

def shell(command: str, working_dir: str = None, timeout: int = 30,
          capture_stderr: bool = True) -> dict:
    """Execute shell command.

    Args:
        command: Shell command to execute
        working_dir: Working directory for command execution (optional)
        timeout: Maximum execution time in seconds (default: 30, max: 300)
        capture_stderr: Whether to capture stderr separately (default: True)

    Returns:
        Dictionary with stdout, stderr, exit_code, and execution_time
    """
    import subprocess
    import time
    from pathlib import Path

    # Validate timeout
    if timeout <= 0:
        timeout = 30
    elif timeout > 300:
        timeout = 300

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
                "timed_out": False
            }

    # Validate working directory if specified
    if working_dir:
        wd = Path(working_dir).resolve()
        if not wd.exists():
            return {
                "stdout": "",
                "stderr": f"Error: Working directory not found: {working_dir}",
                "exit_code": -1,
                "execution_time": 0.0,
                "timed_out": False
            }
        if not wd.is_dir():
            return {
                "stdout": "",
                "stderr": f"Error: Not a directory: {working_dir}",
                "exit_code": -1,
                "execution_time": 0.0,
                "timed_out": False
            }
        working_dir = str(wd)

    start = time.time()
    timed_out = False

    try:
        result = subprocess.run(
            command,
            shell=True,
            cwd=working_dir,
            capture_output=True,
            text=True,
            timeout=timeout
        )

        stdout = result.stdout
        stderr = result.stderr if capture_stderr else ""
        exit_code = result.returncode

    except subprocess.TimeoutExpired as e:
        timed_out = True
        stdout = e.stdout if e.stdout else ""
        stderr = e.stderr if e.stderr and capture_stderr else ""
        exit_code = -1

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

    return {
        "stdout": stdout,
        "stderr": stderr,
        "exit_code": exit_code,
        "execution_time": round(execution_time, 3),
        "timed_out": timed_out
    }
