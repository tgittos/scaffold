"""Search git history to understand code evolution.

Gate: file_read
Match: path
"""

import subprocess
import os

MAX_DIFF_LINES = 80
MAX_RESULTS_DEFAULT = 20


def _clean_env():
    return {k: v for k, v in os.environ.items()
            if k not in ("PYTHONHOME", "PYTHONDONTWRITEBYTECODE")}


def _git(args, cwd, timeout=30):
    result = subprocess.run(
        ["git"] + args,
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=_clean_env(),
    )
    return result.stdout, result.stderr, result.returncode


def _find_repo_root(path):
    p = os.path.abspath(path)
    if os.path.isfile(p):
        p = os.path.dirname(p)
    out, _, rc = _git(["rev-parse", "--show-toplevel"], cwd=p)
    if rc != 0:
        return None
    return out.strip()


def _parse_log_output(stdout, max_results):
    """Parse git log --format output into structured results."""
    results = []
    for block in stdout.strip().split("\x00"):
        if not block.strip():
            continue
        lines = block.strip().split("\n")
        if len(lines) < 3:
            continue
        results.append({
            "hash": lines[0],
            "author": lines[1],
            "date": lines[2],
            "subject": lines[3] if len(lines) > 3 else "",
        })
        if len(results) >= max_results:
            break
    return results


def _op_log(repo_root, file_path, max_results, author, since):
    args = ["log", "--format=%H%n%an%n%ad%n%s%x00", "--date=short",
            f"-{max_results}"]
    if author:
        args.append(f"--author={author}")
    if since:
        args.append(f"--since={since}")
    if file_path:
        args.extend(["--", file_path])
    out, err, rc = _git(args, cwd=repo_root)
    if rc != 0:
        return {"error": err.strip()}
    results = _parse_log_output(out, max_results)
    # Add files changed per commit
    for r in results:
        fout, _, _ = _git(["diff-tree", "--no-commit-id", "--name-only", "-r",
                           r["hash"]], cwd=repo_root)
        r["files"] = [f for f in fout.strip().split("\n") if f]
    return {"operation": "log", "results": results, "total": len(results)}


def _op_pickaxe(repo_root, pattern, file_path, max_results, regex):
    flag = "-G" if regex else "-S"
    args = ["log", "--format=%H%n%an%n%ad%n%s%x00", "--date=short",
            f"-{max_results}", flag, pattern]
    if file_path:
        args.extend(["--", file_path])
    out, err, rc = _git(args, cwd=repo_root)
    if rc != 0:
        return {"error": err.strip()}
    results = _parse_log_output(out, max_results)
    # Add diff excerpts
    for r in results:
        diff_args = ["show", "--format=", "--stat", r["hash"]]
        if file_path:
            diff_args.extend(["--", file_path])
        dout, _, _ = _git(diff_args, cwd=repo_root)
        lines = dout.strip().split("\n")
        r["diff_excerpt"] = "\n".join(lines[:MAX_DIFF_LINES])
        if len(lines) > MAX_DIFF_LINES:
            r["diff_excerpt"] += f"\n... ({len(lines) - MAX_DIFF_LINES} more lines)"
        fout, _, _ = _git(["diff-tree", "--no-commit-id", "--name-only", "-r",
                           r["hash"]], cwd=repo_root)
        r["files"] = [f for f in fout.strip().split("\n") if f]
    return {"operation": "pickaxe", "results": results, "total": len(results),
            "pattern": pattern}


def _op_blame(repo_root, file_path, line_start, line_end):
    if not file_path:
        return {"error": "file_path is required for blame operation"}
    args = ["blame", "--porcelain"]
    if line_start and line_end:
        args.extend([f"-L{line_start},{line_end}"])
    elif line_start:
        args.extend([f"-L{line_start},{line_start}"])
    args.append(file_path)
    out, err, rc = _git(args, cwd=repo_root)
    if rc != 0:
        return {"error": err.strip()}
    # Parse porcelain blame
    results = []
    current = {}
    for line in out.split("\n"):
        if line.startswith("\t"):
            current["content"] = line[1:]
            results.append(current)
            current = {}
        elif " " in line:
            parts = line.split(" ", 1)
            if len(parts[0]) == 40:  # SHA
                current = {"hash": parts[0][:8]}
                nums = parts[1].split()
                if nums:
                    current["line"] = int(nums[0])
            elif parts[0] == "author":
                current["author"] = parts[1]
            elif parts[0] == "author-time":
                import datetime
                ts = int(parts[1])
                current["date"] = datetime.date.fromtimestamp(ts).isoformat()
            elif parts[0] == "summary":
                current["subject"] = parts[1]
    return {"operation": "blame", "results": results, "total": len(results),
            "file": file_path}


def _op_diff(repo_root, commit_hash, file_path):
    if not commit_hash:
        return {"error": "pattern parameter must contain a commit hash for diff operation"}
    args = ["show", "--stat", "--format=%H%n%an%n%ad%n%s", "--date=short",
            commit_hash]
    if file_path:
        args.extend(["--", file_path])
    out, err, rc = _git(args, cwd=repo_root)
    if rc != 0:
        return {"error": err.strip()}
    lines = out.strip().split("\n")
    if len(lines) < 4:
        return {"error": "unexpected git show output"}
    result = {
        "operation": "diff",
        "hash": lines[0],
        "author": lines[1],
        "date": lines[2],
        "subject": lines[3],
        "diff": "\n".join(lines[4:]),
    }
    diff_lines = lines[4:]
    if len(diff_lines) > MAX_DIFF_LINES * 3:
        result["diff"] = "\n".join(diff_lines[:MAX_DIFF_LINES * 3])
        result["diff"] += f"\n... ({len(diff_lines) - MAX_DIFF_LINES * 3} more lines)"
        result["truncated"] = True
    return result


def search_history(path: str, operation: str = "log", pattern: str = None,
                   file_path: str = None, line_start: int = None,
                   line_end: int = None, max_results: int = 20,
                   author: str = None, since: str = None,
                   regex: bool = False) -> dict:
    """Search git history to understand how code evolved. Use 'pickaxe' to find commits that added or removed a code pattern — this is the most powerful operation. Use 'blame' to see who last changed specific lines. Use 'log' for recent file history. Use 'diff' to inspect a specific commit.

    Args:
        path: Repository path or file path to search in
        operation: One of 'log', 'pickaxe', 'blame', 'diff' (default: log)
        pattern: Code pattern for pickaxe search, or commit hash for diff
        file_path: Filter to a specific file (relative to repo root)
        line_start: Starting line for blame range
        line_end: Ending line for blame range
        max_results: Maximum commits to return (default: 20)
        author: Filter by author name or email
        since: Filter commits after date (e.g. '2 weeks ago', '2024-01-01')
        regex: Use regex matching for pickaxe instead of literal string (default: False)

    Returns:
        Dictionary with operation, results list, and total count
    """
    repo_root = _find_repo_root(path)
    if repo_root is None:
        return {"error": f"Not a git repository: {path}"}

    if operation == "log":
        return _op_log(repo_root, file_path, max_results, author, since)
    elif operation == "pickaxe":
        if not pattern:
            return {"error": "pattern is required for pickaxe operation"}
        return _op_pickaxe(repo_root, pattern, file_path, max_results, regex)
    elif operation == "blame":
        return _op_blame(repo_root, file_path, line_start, line_end)
    elif operation == "diff":
        return _op_diff(repo_root, pattern, file_path)
    else:
        return {"error": f"Unknown operation: {operation}. Use log, pickaxe, blame, or diff"}
