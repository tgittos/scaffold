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
    import os

    # Security check - prevent directory traversal (check BEFORE resolving)
    if '..' in path:
        raise ValueError("Invalid path: directory traversal not allowed")

    # Check content size limit
    content_bytes = content.encode('utf-8')
    if len(content_bytes) > MAX_WRITE_SIZE:
        raise ValueError(f"Content too large: {len(content_bytes)} bytes (max {MAX_WRITE_SIZE})")

    # Try to use verified file I/O for TOCTOU-safe writes
    # This is available when approval gates have verified the path
    try:
        import _ralph_verified_io
        if _ralph_verified_io.has_verified_context():
            # Use the resolved path from the verified context (not re-resolving)
            verified_path = _ralph_verified_io.get_resolved_path()
            if verified_path and _ralph_verified_io.path_matches(path):
                p = Path(verified_path)
                # Create backup if requested and file exists
                if backup and p.exists():
                    timestamp = datetime.now().strftime('%Y%m%d%H%M%S%f')
                    backup_path = f"{p}.{timestamp}.bak"
                    shutil.copy2(p, backup_path)
                # Create parent directories if needed
                p.parent.mkdir(parents=True, exist_ok=True)
                # Use the verified file context for atomic open
                fd = _ralph_verified_io.open_verified(str(p), 'w')
                with os.fdopen(fd, 'w', encoding='utf-8') as f:
                    f.write(content)
                return {
                    "success": True,
                    "path": str(p),
                    "bytes_written": len(content_bytes),
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

    # Create backup if requested and file exists (use unique timestamp-based name)
    if backup and p.exists():
        timestamp = datetime.now().strftime('%Y%m%d%H%M%S%f')
        backup_path = f"{p}.{timestamp}.bak"
        shutil.copy2(p, backup_path)

    # Create parent directories if needed
    p.parent.mkdir(parents=True, exist_ok=True)

    # Fall back to standard file I/O (when gates disabled or no context)
    p.write_text(content, encoding='utf-8')

    return {
        "success": True,
        "path": str(p),
        "bytes_written": len(content_bytes)
    }
