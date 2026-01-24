"""Write content to a file.

Gate: file_write
Match: path
"""

MAX_WRITE_SIZE = 50 * 1024 * 1024  # 50MB

def write_file(path: str, content: str, backup: bool = False) -> dict:
    """Write content to file.

    Args:
        path: Path to the file to write
        content: Content to write
        backup: Whether to create a backup if file exists

    Returns:
        Dictionary with success status and path
    """
    from pathlib import Path
    from datetime import datetime
    import shutil

    # Security check - prevent directory traversal (check BEFORE resolving)
    if '..' in path:
        raise ValueError("Invalid path: directory traversal not allowed")

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

    # Write content
    p.write_text(content, encoding='utf-8')

    return {
        "success": True,
        "path": str(p),
        "bytes_written": len(content_bytes)
    }
