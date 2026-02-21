"""Search for patterns in files.

Gate: file_read
Match: pattern
"""

MAX_FILE_SIZE = 1024 * 1024  # 1MB
CONTEXT_CHARS = 30  # Characters of context around match
MAX_LINE_DISPLAY = 500  # Max characters to display per line
MAX_CONTEXT_LINES = 10  # Maximum surrounding context lines


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def search_files(path: str, pattern: str, glob_filter: str = None,
                 recursive: bool = True, case_sensitive: bool = True,
                 max_results: int = 100, context_lines: int = 0) -> dict:
    """Search for pattern in files.

    Args:
        path: Path to search (file or directory)
        pattern: Regular expression pattern to search for
        glob_filter: Optional glob pattern to filter files (e.g., "*.py", "*.txt")
        recursive: Whether to search recursively in directories (default: True)
        case_sensitive: Whether search is case sensitive (default: True)
        max_results: Maximum number of results to return (default: 100)
        context_lines: Number of surrounding lines to include before and after each match (0-10, default: 0)

    Returns:
        Dictionary with results list, total_matches, total_matches_found,
        matched_files, files_searched, and truncated flag
    """
    from pathlib import Path
    import re
    import os

    # Security check - prevent directory traversal
    if _is_traversal_path(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"Path not found: {path}")

    # Compile regex pattern
    flags = 0 if case_sensitive else re.IGNORECASE
    try:
        regex = re.compile(pattern, flags)
    except re.error as e:
        raise ValueError(f"Invalid regex pattern '{pattern}': {e}")

    # Clamp context_lines
    context_lines = max(0, min(int(context_lines), MAX_CONTEXT_LINES))

    results = []
    files_searched = 0
    total_matches_found = 0
    matched_files_set = set()

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
        nonlocal files_searched, total_matches_found

        try:
            stat = file_path.stat()
            if stat.st_size > MAX_FILE_SIZE:
                return

            content = file_path.read_text(encoding='utf-8', errors='replace')
            files_searched += 1
            all_lines = content.splitlines()

            for line_idx, line in enumerate(all_lines):
                matches = list(regex.finditer(line))
                if not matches:
                    continue

                line_num = line_idx + 1
                total_matches_found += len(matches)
                file_str = str(file_path)
                matched_files_set.add(file_str)

                if len(results) >= max_results:
                    return  # Stop scanning this file once results are full

                # Build match context from first match on this line
                match = matches[0]
                start = max(0, match.start() - CONTEXT_CHARS)
                end = min(len(line), match.end() + CONTEXT_CHARS)
                context = line[start:end]
                if start > 0:
                    context = '...' + context
                if end < len(line):
                    context = context + '...'

                result_entry = {
                    "file_path": file_str,
                    "line_number": line_num,
                    "line_content": line.strip()[:MAX_LINE_DISPLAY],
                    "match_context": context
                }

                if context_lines > 0:
                    surrounding = []
                    ctx_start = max(0, line_idx - context_lines)
                    ctx_end = min(len(all_lines), line_idx + context_lines + 1)
                    for ci in range(ctx_start, ctx_end):
                        if ci == line_idx:
                            continue
                        surrounding.append({
                            "line_number": ci + 1,
                            "content": all_lines[ci][:MAX_LINE_DISPLAY]
                        })
                    result_entry["surrounding_lines"] = surrounding

                results.append(result_entry)
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

                    if glob_filter and not file_path.match(glob_filter):
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

                if glob_filter and not file_path.match(glob_filter):
                    continue

                search_in_file(file_path)

    return {
        "results": results,
        "total_matches": len(results),
        "total_matches_found": total_matches_found,
        "matched_files": sorted(matched_files_set),
        "files_searched": files_searched,
        "truncated": len(results) >= max_results
    }
