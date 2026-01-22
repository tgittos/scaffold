"""Apply delta patch operations to a file."""

def apply_delta(path: str, operations: list, create_backup: bool = True) -> dict:
    """Apply delta patch operations to a file.

    Args:
        path: Path to the file to modify
        operations: List of delta operations. Each operation is a dict with:
            - type: "insert", "delete", or "replace"
            - start_line: 1-based line number
            - line_count: Number of lines affected (for delete/replace)
            - lines: List of lines to insert/replace (for insert/replace)
        create_backup: Whether to create a backup before modifying

    Returns:
        Dictionary with success status and details
    """
    from pathlib import Path
    import shutil

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"File not found: {path}")

    if not p.is_file():
        raise ValueError(f"Not a file: {path}")

    # Security check
    if '..' in str(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    # Read current content
    content = p.read_text(encoding='utf-8')
    lines = content.splitlines(keepends=True)

    # Ensure last line has newline
    if lines and not lines[-1].endswith('\n'):
        lines[-1] += '\n'

    # Create backup if requested
    backup_path = None
    if create_backup:
        backup_path = str(p) + '.bak'
        shutil.copy2(p, backup_path)

    # Sort operations by start_line in reverse order
    # This ensures we apply changes from bottom to top, preserving line numbers
    sorted_ops = sorted(operations, key=lambda x: x.get('start_line', 0), reverse=True)

    operations_applied = 0

    for op in sorted_ops:
        op_type = op.get('type', '').lower()
        start_line = op.get('start_line', 1)
        line_count = op.get('line_count', 0)
        new_lines = op.get('lines', [])

        # Convert to 0-based index
        idx = start_line - 1

        if idx < 0:
            continue

        if op_type == 'insert':
            # Insert lines at position
            insert_content = []
            for line in new_lines:
                if not line.endswith('\n'):
                    line += '\n'
                insert_content.append(line)
            lines[idx:idx] = insert_content
            operations_applied += 1

        elif op_type == 'delete':
            # Delete lines in range
            end_idx = min(idx + line_count, len(lines))
            del lines[idx:end_idx]
            operations_applied += 1

        elif op_type == 'replace':
            # Replace lines in range
            end_idx = min(idx + line_count, len(lines))
            replace_content = []
            for line in new_lines:
                if not line.endswith('\n'):
                    line += '\n'
                replace_content.append(line)
            lines[idx:end_idx] = replace_content
            operations_applied += 1

    # Write result
    result_content = ''.join(lines)
    p.write_text(result_content, encoding='utf-8')

    return {
        "success": True,
        "path": str(p),
        "operations_applied": operations_applied,
        "backup_path": backup_path,
        "new_line_count": len(lines)
    }
