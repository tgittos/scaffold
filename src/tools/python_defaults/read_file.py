"""Read file contents with optional line range.

Gate: file_read
Match: path
"""


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def read_file(path: str, start_line: int = 1, end_line: int = None) -> dict:
    """Read file contents, optionally with line range.

    Args:
        path: Path to the file to read
        start_line: Starting line number (1-based, default: 1 for first line)
        end_line: Ending line number (1-based inclusive, default: None for end of file)

    Returns:
        Dictionary with content (line-numbered text), total_lines, and range
    """
    from pathlib import Path
    import os

    # Security check - prevent directory traversal
    if _is_traversal_path(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    # Try to use verified file I/O for TOCTOU-safe reads
    # This is available when approval gates have verified the path
    content = None
    try:
        import _ralph_verified_io
        if _ralph_verified_io.has_verified_context():
            # Use the resolved path from the verified context (not re-resolving)
            verified_path = _ralph_verified_io.get_resolved_path()
            if verified_path and _ralph_verified_io.path_matches(path):
                p = Path(verified_path)
                if not p.exists():
                    raise FileNotFoundError(f"File not found: {path}")
                if not p.is_file():
                    raise ValueError(f"Not a file: {path}")
                # Check file size (1MB limit)
                max_size = 1024 * 1024
                if p.stat().st_size > max_size:
                    raise ValueError(f"File too large (>{max_size} bytes): {path}")
                # Use the verified file context for atomic open
                fd = _ralph_verified_io.open_verified(str(p), 'r')
                with os.fdopen(fd, 'r', encoding='utf-8', errors='replace') as f:
                    content = f.read()
    except ImportError:
        # Module not available - fall through to standard I/O
        pass
    except OSError as e:
        # Verification failed - report the error
        raise OSError(f"File verification failed: {e}")

    # Standard path resolution when verified context not available
    if content is None:
        p = Path(path).resolve()

        if not p.exists():
            raise FileNotFoundError(f"File not found: {path}")

        if not p.is_file():
            raise ValueError(f"Not a file: {path}")

        # Check file size (1MB limit)
        max_size = 1024 * 1024
        if p.stat().st_size > max_size:
            raise ValueError(f"File too large (>{max_size} bytes): {path}")

    # Fall back to standard file I/O (when gates disabled or no context)
    if content is None:
        content = p.read_text(encoding='utf-8', errors='replace')

    all_lines = content.splitlines(keepends=True)
    total_lines = len(all_lines)

    # Apply line range if specified
    start = max(0, start_line - 1)  # Convert to 0-based
    end = end_line if end_line is not None else total_lines
    end = min(end, total_lines)
    selected = all_lines[start:end]

    # Build line-numbered content
    numbered = []
    for i, line in enumerate(selected, start=start + 1):
        # Strip trailing newline for consistent formatting, then re-add
        numbered.append(f"{i}: {line.rstrip(chr(10)).rstrip(chr(13))}")
    numbered_content = '\n'.join(numbered)
    if numbered:
        numbered_content += '\n'

    range_str = f"{start + 1}-{end}" if total_lines > 0 else "0-0"

    return {
        "success": True,
        "content": numbered_content,
        "total_lines": total_lines,
        "range": range_str
    }
