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

    # Security check - prevent directory traversal (check BEFORE resolving)
    if '..' in path:
        raise ValueError("Invalid path: directory traversal not allowed")

    # Check content size limit
    content_bytes = content.encode('utf-8')
    if len(content_bytes) > MAX_APPEND_SIZE:
        raise ValueError(f"Content too large: {len(content_bytes)} bytes (max {MAX_APPEND_SIZE})")

    p = Path(path).resolve()

    # Create parent directories if needed
    p.parent.mkdir(parents=True, exist_ok=True)

    # Append content
    with open(p, 'a', encoding='utf-8') as f:
        f.write(content)

    return {
        "success": True,
        "path": str(p),
        "bytes_appended": len(content_bytes)
    }
