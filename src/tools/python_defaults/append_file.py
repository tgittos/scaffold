"""Append content to a file.

Gate: file_write
Match: path
"""

MAX_APPEND_SIZE = 10 * 1024 * 1024  # 10MB

def append_file(path: str, content: str) -> dict:
    """Append content to file.

    Args:
        path: Path to the file to append to
        content: Content to append

    Returns:
        Dictionary with success status and path
    """
    from pathlib import Path
    import os

    # Security check - prevent directory traversal (check BEFORE resolving)
    if '..' in path:
        raise ValueError("Invalid path: directory traversal not allowed")

    # Check content size limit
    content_bytes = content.encode('utf-8')
    if len(content_bytes) > MAX_APPEND_SIZE:
        raise ValueError(f"Content too large: {len(content_bytes)} bytes (max {MAX_APPEND_SIZE})")

    # Try to use verified file I/O for TOCTOU-safe appends
    # This is available when approval gates have verified the path
    try:
        import _ralph_verified_io
        if _ralph_verified_io.has_verified_context():
            # Use the resolved path from the verified context (not re-resolving)
            verified_path = _ralph_verified_io.get_resolved_path()
            if verified_path and _ralph_verified_io.path_matches(path):
                p = Path(verified_path)
                # Create parent directories if needed
                p.parent.mkdir(parents=True, exist_ok=True)
                # Use the verified file context for atomic open
                fd = _ralph_verified_io.open_verified(str(p), 'a')
                with os.fdopen(fd, 'a', encoding='utf-8') as f:
                    f.write(content)
                return {
                    "success": True,
                    "path": str(p),
                    "bytes_appended": len(content_bytes),
                    "verified": True
                }
    except ImportError:
        # Module not available - fall through to standard I/O
        pass
    except OSError as e:
        # Verification failed - report the error
        raise OSError(f"File verification failed: {e}")

    # Standard path resolution when verified context not available
    p = Path(path).resolve()

    # Create parent directories if needed
    p.parent.mkdir(parents=True, exist_ok=True)

    # Fall back to standard file I/O (when gates disabled or no context)
    with open(p, 'a', encoding='utf-8') as f:
        f.write(content)

    return {
        "success": True,
        "path": str(p),
        "bytes_appended": len(content_bytes)
    }
