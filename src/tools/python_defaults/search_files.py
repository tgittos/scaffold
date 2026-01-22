"""Search for patterns in files."""

def search_files(path: str, pattern: str, file_pattern: str = None,
                 recursive: bool = True, case_sensitive: bool = True,
                 max_results: int = 100) -> dict:
    """Search for pattern in files.

    Args:
        path: Path to search (file or directory)
        pattern: Pattern to search for (supports regex)
        file_pattern: Optional file pattern filter (e.g., "*.py")
        recursive: Whether to search recursively in directories
        case_sensitive: Whether search is case sensitive
        max_results: Maximum number of results to return

    Returns:
        List of dictionaries with match information
    """
    from pathlib import Path
    import re
    import os

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"Path not found: {path}")

    # Compile regex pattern
    flags = 0 if case_sensitive else re.IGNORECASE
    try:
        regex = re.compile(pattern, flags)
    except re.error as e:
        raise ValueError(f"Invalid regex pattern: {e}")

    results = []
    files_searched = 0
    max_file_size = 1024 * 1024  # 1MB

    # Directories to skip
    skip_dirs = {'.git', '.svn', '.hg', 'node_modules', '__pycache__',
                 '.cache', 'build', 'dist', 'deps', 'vendor', '.venv', 'venv'}

    # Binary extensions to skip
    binary_exts = {'.exe', '.dll', '.so', '.dylib', '.a', '.o', '.obj',
                   '.zip', '.tar', '.gz', '.bz2', '.xz', '.7z', '.rar',
                   '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.ico', '.svg',
                   '.mp3', '.mp4', '.avi', '.mov', '.mkv', '.wav',
                   '.pdf', '.doc', '.docx', '.xls', '.xlsx',
                   '.pyc', '.pyo', '.class', '.wasm', '.db', '.sqlite'}

    def should_skip_file(file_path):
        return file_path.suffix.lower() in binary_exts

    def search_in_file(file_path):
        nonlocal files_searched

        if len(results) >= max_results:
            return

        try:
            stat = file_path.stat()
            if stat.st_size > max_file_size:
                return

            content = file_path.read_text(encoding='utf-8', errors='replace')
            files_searched += 1

            for line_num, line in enumerate(content.splitlines(), 1):
                if len(results) >= max_results:
                    return

                match = regex.search(line)
                if match:
                    # Get context (surrounding characters)
                    start = max(0, match.start() - 30)
                    end = min(len(line), match.end() + 30)
                    context = line[start:end]
                    if start > 0:
                        context = '...' + context
                    if end < len(line):
                        context = context + '...'

                    results.append({
                        "file_path": str(file_path),
                        "line_number": line_num,
                        "line_content": line.strip()[:200],
                        "match_context": context
                    })
        except (PermissionError, OSError, UnicodeDecodeError):
            pass  # Skip files we can't read

    if p.is_file():
        if not should_skip_file(p):
            search_in_file(p)
    else:
        if recursive:
            for root, dirs, files in os.walk(p):
                # Filter out directories to skip
                dirs[:] = [d for d in dirs if d not in skip_dirs]

                for name in files:
                    if len(results) >= max_results:
                        break

                    file_path = Path(root) / name

                    if should_skip_file(file_path):
                        continue

                    if file_pattern and not file_path.match(file_pattern):
                        continue

                    search_in_file(file_path)

                if len(results) >= max_results:
                    break
        else:
            for file_path in p.iterdir():
                if len(results) >= max_results:
                    break

                if not file_path.is_file():
                    continue

                if should_skip_file(file_path):
                    continue

                if file_pattern and not file_path.match(file_pattern):
                    continue

                search_in_file(file_path)

    return {
        "results": results,
        "total_matches": len(results),
        "files_searched": files_searched,
        "truncated": len(results) >= max_results
    }
