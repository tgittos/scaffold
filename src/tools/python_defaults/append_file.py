"""Append content to a file."""

def append_file(path: str, content: str) -> dict:
    """Append content to file.

    Args:
        path: Path to the file to append to
        content: Content to append

    Returns:
        Dictionary with success status and path
    """
    from pathlib import Path

    p = Path(path).resolve()

    # Security check - prevent directory traversal
    if '..' in str(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    # Create parent directories if needed
    p.parent.mkdir(parents=True, exist_ok=True)

    # Append content
    with open(p, 'a', encoding='utf-8') as f:
        f.write(content)

    return {
        "success": True,
        "path": str(p),
        "bytes_appended": len(content.encode('utf-8'))
    }
