"""Get file information and metadata."""

def file_info(path: str) -> dict:
    """Get file information and metadata.

    Args:
        path: Path to the file or directory

    Returns:
        Dictionary with file information
    """
    from pathlib import Path
    import os
    import stat as stat_module
    from datetime import datetime

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"Path not found: {path}")

    st = p.stat()

    # Format permissions as octal string
    perms = stat_module.filemode(st.st_mode)

    return {
        "path": str(p),
        "name": p.name,
        "size": st.st_size,
        "is_directory": p.is_dir(),
        "is_file": p.is_file(),
        "is_symlink": p.is_symlink(),
        "permissions": perms,
        "permissions_octal": oct(st.st_mode)[-3:],
        "owner_uid": st.st_uid,
        "group_gid": st.st_gid,
        "modified_time": datetime.fromtimestamp(st.st_mtime).isoformat(),
        "created_time": datetime.fromtimestamp(st.st_ctime).isoformat(),
        "accessed_time": datetime.fromtimestamp(st.st_atime).isoformat(),
        "is_readable": os.access(p, os.R_OK),
        "is_writable": os.access(p, os.W_OK),
        "is_executable": os.access(p, os.X_OK)
    }
