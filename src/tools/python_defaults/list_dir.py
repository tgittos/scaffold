"""List directory contents.

Gate: file_read
Match: path
"""


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def list_dir(path: str, glob_filter: str = None, recursive: bool = False, include_hidden: bool = False) -> list:
    """List directory contents with optional filtering.

    Args:
        path: Path to the directory to list
        glob_filter: Optional glob pattern to filter results (e.g., "*.py", "*.txt")
        recursive: Whether to list recursively (default: False)
        include_hidden: Whether to include hidden files starting with . (default: False)

    Returns:
        List of dictionaries with file/directory information
    """
    from pathlib import Path
    from datetime import datetime, timezone
    import os

    # Security check - prevent directory traversal
    if _is_traversal_path(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"Directory not found: {path}")

    if not p.is_dir():
        raise ValueError(f"Not a directory: {path}")

    results = []
    max_entries = 1000

    # Directories to skip in recursive mode
    skip_dirs = {'.git', '.svn', '.hg', 'node_modules', '__pycache__',
                 '.cache', 'build', 'dist', 'deps', 'vendor', '.venv', 'venv'}

    def should_include(entry_path):
        name = entry_path.name
        if not include_hidden and name.startswith('.'):
            return False
        return True

    def add_entry(entry_path):
        if len(results) >= max_entries:
            return False

        try:
            stat = entry_path.stat()
            mtime_iso = datetime.fromtimestamp(
                stat.st_mtime, tz=timezone.utc
            ).isoformat()
            results.append({
                "name": entry_path.name,
                "path": str(entry_path),
                "is_directory": entry_path.is_dir(),
                "size": stat.st_size if not entry_path.is_dir() else 0,
                "modified_time": mtime_iso
            })
            return True
        except (PermissionError, OSError):
            return True  # Continue even if we can't stat this entry

    if recursive:
        if glob_filter:
            for entry in p.rglob(glob_filter):
                if not should_include(entry):
                    continue
                # Skip common non-essential directories
                parts = entry.parts
                if any(skip in parts for skip in skip_dirs):
                    continue
                if not add_entry(entry):
                    break
        else:
            for root, dirs, files in os.walk(p):
                root_path = Path(root)

                # Filter out directories we want to skip
                dirs[:] = [d for d in dirs if d not in skip_dirs and
                          (include_hidden or not d.startswith('.'))]

                for name in files:
                    if not include_hidden and name.startswith('.'):
                        continue
                    if not add_entry(root_path / name):
                        break

                for name in dirs:
                    if not add_entry(root_path / name):
                        break

                if len(results) >= max_entries:
                    break
    else:
        if glob_filter:
            entries = list(p.glob(glob_filter))
        else:
            entries = list(p.iterdir())

        for entry in sorted(entries, key=lambda x: (not x.is_dir(), x.name.lower())):
            if not should_include(entry):
                continue
            if not add_entry(entry):
                break

    return results
