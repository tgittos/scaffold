"""Write content to a file.

Thread-safe: true
Match: path
"""

MAX_WRITE_SIZE = 50 * 1024 * 1024  # 50MB


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def write_file(path: str, content: str, mode: str = "write", backup: bool = False) -> dict:
    """Create a new file or overwrite/append to an existing file. For surgical edits to existing files, prefer apply_patch — it is safer and preserves surrounding code. Calls in the same batch run concurrently; put steps that depend on each other in separate turns.

    Args:
        path: Path to the file to write
        content: Content to write
        mode: "write" to overwrite or "append" to append (default: "write")
        backup: Whether to create a backup if file exists (default: False)

    Returns:
        Dictionary with success status and path
    """
    from pathlib import Path
    from datetime import datetime
    import shutil

    # Security check - prevent directory traversal
    if _is_traversal_path(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    # Validate mode
    if mode not in ("write", "append"):
        raise ValueError(f"Invalid mode: {mode} (must be 'write' or 'append')")

    bytes_key = "bytes_appended" if mode == "append" else "bytes_written"

    # Check content size limit
    content_bytes = content.encode('utf-8')
    if len(content_bytes) > MAX_WRITE_SIZE:
        raise ValueError(f"Content too large: {len(content_bytes)} bytes (max {MAX_WRITE_SIZE})")

    p = Path(path).resolve()

    # Create backup if requested and file exists (use unique timestamp-based name)
    if backup and p.exists():
        timestamp = datetime.now().strftime('%Y%m%d%H%M%S%f')
        backup_path = f"{p}.{timestamp}.bak"
        shutil.copy2(p, backup_path)

    # Create parent directories if needed
    p.parent.mkdir(parents=True, exist_ok=True)

    if mode == "append":
        with open(p, 'a', encoding='utf-8') as f:
            f.write(content)
    else:
        p.write_text(content, encoding='utf-8')

    return {
        "success": True,
        "path": str(p),
        bytes_key: len(content_bytes)
    }
