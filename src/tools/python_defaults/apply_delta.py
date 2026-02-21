"""Apply delta patch operations to a file.

Gate: file_write
Match: path
"""

VALID_OP_TYPES = {'insert', 'delete', 'replace'}


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    import os
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def _validate_operations(operations: list) -> None:
    """Validate operation structure before processing.

    Provides detailed error messages to help AI agents self-correct.
    """
    if not isinstance(operations, list):
        raise ValueError(
            f"operations must be a list of delta objects, got {type(operations).__name__}. "
            f"Example: [{{'type': 'insert', 'start_line': 5, 'content': ['new line']}}]"
        )

    for i, op in enumerate(operations):
        if not isinstance(op, dict):
            raise ValueError(
                f"Operation {i} must be a dictionary, got {type(op).__name__}. "
                f"Received: {repr(op)[:200]}"
            )

        if 'type' not in op:
            raise ValueError(
                f"Operation {i} missing required 'type' field. "
                f"Must be one of: {VALID_OP_TYPES}. Received: {op}"
            )

        op_type = op.get('type', '').lower()
        if op_type not in VALID_OP_TYPES:
            raise ValueError(
                f"Operation {i} has invalid type '{op.get('type')}'. "
                f"Must be one of: {VALID_OP_TYPES}"
            )

        if 'start_line' not in op:
            raise ValueError(
                f"Operation {i} ({op_type}) missing required 'start_line' field. "
                f"Received: {op}"
            )

        start_line = op.get('start_line')
        if not isinstance(start_line, int) or start_line < 1:
            raise ValueError(
                f"Operation {i} ({op_type}) has invalid start_line: {start_line}. "
                f"Must be a positive integer (1-based line numbering)"
            )

        # Validate end_line for delete and replace
        if op_type in ('delete', 'replace'):
            if 'end_line' not in op:
                raise ValueError(
                    f"Operation {i} ({op_type}) missing required 'end_line' field. "
                    f"end_line specifies the last line to {op_type} (inclusive). "
                    f"Example: {{'type': '{op_type}', 'start_line': 5, 'end_line': 7, ...}}"
                )
            end_line = op.get('end_line')
            if not isinstance(end_line, int) or end_line < 1:
                raise ValueError(
                    f"Operation {i} ({op_type}) has invalid end_line: {end_line}. "
                    f"Must be a positive integer (1-based line numbering)"
                )
            if end_line < start_line:
                raise ValueError(
                    f"Operation {i} ({op_type}) has end_line ({end_line}) < start_line ({start_line}). "
                    f"end_line must be >= start_line"
                )

        # Validate content for insert and replace
        if op_type in ('insert', 'replace'):
            if 'content' not in op:
                raise ValueError(
                    f"Operation {i} ({op_type}) missing required 'content' field. "
                    f"content must be a list of strings (lines to {op_type}). "
                    f"Example: {{'type': '{op_type}', 'start_line': 5, 'content': ['line 1', 'line 2']}}"
                )
            content = op.get('content')
            if not isinstance(content, list):
                raise ValueError(
                    f"Operation {i} ({op_type}) has invalid content: must be a list of strings, "
                    f"got {type(content).__name__}. "
                    f"Example: {{'content': ['line 1', 'line 2']}}"
                )
            for j, line in enumerate(content):
                if not isinstance(line, str):
                    raise ValueError(
                        f"Operation {i} ({op_type}) content[{j}] must be a string, "
                        f"got {type(line).__name__}: {repr(line)[:100]}"
                    )

        # Validate optional expected field for replace and delete
        if op_type in ('replace', 'delete') and 'expected' in op:
            expected = op['expected']
            if not isinstance(expected, list):
                raise ValueError(
                    f"Operation {i} ({op_type}) has invalid expected: must be a list of strings, "
                    f"got {type(expected).__name__}"
                )
            for j, line in enumerate(expected):
                if not isinstance(line, str):
                    raise ValueError(
                        f"Operation {i} ({op_type}) expected[{j}] must be a string, "
                        f"got {type(line).__name__}: {repr(line)[:100]}"
                    )


def apply_delta(path: str, operations: list, create_backup: bool = True) -> dict:
    """Apply delta patch operations to a file.

    Args:
        path: Path to the file to modify
        operations: List of delta operations. Each operation is a dict with:
            - type: "insert", "delete", or "replace"
            - start_line: 1-based line number where operation begins
            - end_line: 1-based line number where operation ends (inclusive, required for delete/replace)
            - content: List of strings (lines to insert/replace, required for insert/replace)
            For insert: inserts content BEFORE start_line (use start_line=1 to insert at beginning)
            For delete: removes lines from start_line to end_line (inclusive)
            For replace: replaces lines from start_line to end_line with content
        create_backup: Whether to create a backup before modifying (default: True)

    Returns:
        Dictionary with success status, path, operations_applied, backup_path, and new_line_count
    """
    from pathlib import Path
    from datetime import datetime
    import shutil

    # Security check - prevent directory traversal
    if _is_traversal_path(path):
        raise ValueError("Invalid path: directory traversal not allowed")

    # Validate operations structure before any file operations
    _validate_operations(operations)

    p = Path(path).resolve()

    if not p.exists():
        raise FileNotFoundError(f"File not found: {path}")

    if not p.is_file():
        raise ValueError(f"Not a file: {path}")

    # Read current content
    content = p.read_text(encoding='utf-8')
    lines = content.splitlines(keepends=True)

    # Ensure last line has newline
    if lines and not lines[-1].endswith('\n'):
        lines[-1] += '\n'

    # Create backup if requested (use unique timestamp-based name)
    backup_path = None
    if create_backup:
        timestamp = datetime.now().strftime('%Y%m%d%H%M%S%f')
        backup_path = f"{p}.{timestamp}.bak"
        shutil.copy2(p, backup_path)

    # Sort operations by start_line in reverse order
    # This ensures we apply changes from bottom to top, preserving line numbers
    sorted_ops = sorted(operations, key=lambda x: x.get('start_line', 0), reverse=True)

    # Verify all expected content BEFORE applying any mutations
    for op in sorted_ops:
        op_type = op.get('type', '').lower()
        if op_type in ('replace', 'delete') and 'expected' in op:
            start_line = op.get('start_line', 1)
            idx = start_line - 1
            expected = op['expected']
            end_line = op.get('end_line', start_line)
            end_idx = min(end_line, len(lines))
            actual = [l.rstrip('\n').rstrip('\r') for l in lines[idx:end_idx]]
            if actual != expected:
                raise ValueError(
                    f"Content mismatch at lines {start_line}-{end_line}. "
                    f"Expected {len(expected)} lines:\n"
                    + '\n'.join(f"  {s}" for s in expected[:5])
                    + ('\n  ...' if len(expected) > 5 else '')
                    + f"\nActual {len(actual)} lines:\n"
                    + '\n'.join(f"  {s}" for s in actual[:5])
                    + ('\n  ...' if len(actual) > 5 else '')
                    + "\nThe file may have changed since it was last read."
                )

    operations_applied = 0

    for op in sorted_ops:
        op_type = op.get('type', '').lower()
        start_line = op.get('start_line', 1)
        new_content = op.get('content', [])

        # Convert to 0-based index
        idx = start_line - 1

        if idx < 0:
            continue

        if op_type == 'insert':
            # Insert lines BEFORE the specified line
            insert_lines = []
            for line in new_content:
                if not line.endswith('\n'):
                    line += '\n'
                insert_lines.append(line)
            lines[idx:idx] = insert_lines
            operations_applied += 1

        elif op_type == 'delete':
            # Delete lines from start_line to end_line (inclusive)
            end_line = op.get('end_line', start_line)
            end_idx = min(end_line, len(lines))  # end_line is 1-based, inclusive
            del lines[idx:end_idx]
            operations_applied += 1

        elif op_type == 'replace':
            # Replace lines from start_line to end_line (inclusive) with content
            end_line = op.get('end_line', start_line)
            end_idx = min(end_line, len(lines))  # end_line is 1-based, inclusive
            replace_lines = []
            for line in new_content:
                if not line.endswith('\n'):
                    line += '\n'
                replace_lines.append(line)
            lines[idx:end_idx] = replace_lines
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
