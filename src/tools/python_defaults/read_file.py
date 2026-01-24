"""Read file contents with optional line range.

Gate: file_read
Match: path
"""

def read_file(path: str, start_line: int = 0, end_line: int = 0) -> str:
    """Read file contents, optionally with line range.

    Args:
        path: Path to the file to read
        start_line: Starting line number (1-based, 0 for entire file)
        end_line: Ending line number (1-based, 0 for to end of file)

    Returns:
        File contents as a string
    """
    from pathlib import Path

    # Security check - prevent directory traversal (check BEFORE resolving)
    if '..' in path:
        raise ValueError("Invalid path: directory traversal not allowed")

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"File not found: {path}")

    if not p.is_file():
        raise ValueError(f"Not a file: {path}")

    # Check file size (1MB limit)
    max_size = 1024 * 1024
    if p.stat().st_size > max_size:
        raise ValueError(f"File too large (>{max_size} bytes): {path}")

    content = p.read_text(encoding='utf-8', errors='replace')

    if start_line > 0 or end_line > 0:
        lines = content.splitlines(keepends=True)
        start = max(0, start_line - 1) if start_line > 0 else 0
        end = end_line if end_line > 0 else len(lines)
        content = ''.join(lines[start:end])

    return content
