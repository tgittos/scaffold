"""Write content to a file."""

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
    import shutil

    p = Path(path).resolve()

    # Security check - prevent directory traversal
    if '..' in str(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    # Create backup if requested and file exists
    if backup and p.exists():
        backup_path = str(p) + '.bak'
        shutil.copy2(p, backup_path)

    # Create parent directories if needed
    p.parent.mkdir(parents=True, exist_ok=True)

    # Write content
    p.write_text(content, encoding='utf-8')

    return {
        "success": True,
        "path": str(p),
        "bytes_written": len(content.encode('utf-8'))
    }
