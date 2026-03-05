"""Apply text patches to files using a unified-diff-like format.

Gate: file_write
Match: patch
"""


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def _find_anchor(lines, anchor, start=0):
    """Find the first line matching anchor text, starting from start index."""
    anchor_stripped = anchor.strip()
    for i in range(start, len(lines)):
        if lines[i].rstrip('\n').rstrip('\r').strip() == anchor_stripped:
            return i
    return -1


def _apply_hunk(lines, hunk_lines, file_path):
    """Apply a single hunk (set of changes with optional @@ anchors) to lines.

    Returns modified lines list.
    """
    # Parse the hunk into segments separated by @@ anchors
    # Each segment has: anchor (optional), context/add/remove lines
    segments = []
    current_anchor = None
    current_ops = []

    for line in hunk_lines:
        if line.startswith('@@'):
            if current_ops:
                segments.append((current_anchor, current_ops))
                current_ops = []
            current_anchor = line[2:].strip()
        else:
            current_ops.append(line)

    if current_ops:
        segments.append((current_anchor, current_ops))

    if not segments:
        return lines

    # For nested @@ anchors, each narrows the search scope
    search_start = 0

    for anchor, ops in segments:
        if anchor:
            anchor_idx = _find_anchor(lines, anchor, search_start)
            if anchor_idx == -1:
                raise ValueError(
                    f"Anchor not found in {file_path}: '{anchor}'. "
                    f"The file may have changed since you last read it. "
                    f"Use read_file to get current contents."
                )
            # Start applying changes after the anchor line
            search_start = anchor_idx + 1

        # Now apply the operations starting from search_start
        # We need to match context lines to find exact position
        pos = search_start

        # Find the position by matching leading context lines
        context_lines = []
        for op_line in ops:
            if op_line.startswith(' '):
                context_lines.append(op_line[1:])
            else:
                break

        if context_lines:
            # Search for context match starting from pos
            found = False
            for try_pos in range(pos, len(lines)):
                match = True
                for ci, ctx in enumerate(context_lines):
                    if try_pos + ci >= len(lines):
                        match = False
                        break
                    actual = lines[try_pos + ci].rstrip('\n').rstrip('\r')
                    if actual != ctx.rstrip('\n').rstrip('\r'):
                        match = False
                        break
                if match:
                    pos = try_pos
                    found = True
                    break
            if not found:
                # Try from beginning if anchor didn't help
                if search_start > 0:
                    for try_pos in range(0, search_start):
                        match = True
                        for ci, ctx in enumerate(context_lines):
                            if try_pos + ci >= len(lines):
                                match = False
                                break
                            actual = lines[try_pos + ci].rstrip('\n').rstrip('\r')
                            if actual != ctx.rstrip('\n').rstrip('\r'):
                                match = False
                                break
                        if match:
                            pos = try_pos
                            found = True
                            break
                if not found:
                    expected_ctx = '\n'.join(f'  {c}' for c in context_lines[:5])
                    raise ValueError(
                        f"Context mismatch in {file_path}. Expected:\n{expected_ctx}\n"
                        f"The file may have changed. Use read_file to get current contents."
                    )

        # Apply operations at pos
        new_lines = []
        i = 0  # index into ops
        file_idx = pos  # current position in file

        # Add everything before pos
        new_lines.extend(lines[:pos])

        while i < len(ops):
            op_line = ops[i]
            if op_line.startswith(' '):
                # Context line - verify and keep
                expected = op_line[1:]
                if file_idx < len(lines):
                    actual = lines[file_idx].rstrip('\n').rstrip('\r')
                    exp_clean = expected.rstrip('\n').rstrip('\r')
                    if actual != exp_clean:
                        raise ValueError(
                            f"Context mismatch in {file_path} at line {file_idx + 1}. "
                            f"Expected: '{exp_clean}'\n"
                            f"Actual:   '{actual}'\n"
                            f"Use read_file to get current contents."
                        )
                    new_lines.append(lines[file_idx])
                    file_idx += 1
                else:
                    raise ValueError(
                        f"Context mismatch in {file_path}: file ended at line {file_idx + 1} "
                        f"but expected: '{expected.rstrip()}'"
                    )
                i += 1
            elif op_line.startswith('-'):
                # Remove line - verify content matches
                removed = op_line[1:]
                if file_idx < len(lines):
                    actual = lines[file_idx].rstrip('\n').rstrip('\r')
                    exp_clean = removed.rstrip('\n').rstrip('\r')
                    if actual != exp_clean:
                        raise ValueError(
                            f"Remove mismatch in {file_path} at line {file_idx + 1}. "
                            f"Expected to remove: '{exp_clean}'\n"
                            f"Actual line:        '{actual}'\n"
                            f"Use read_file to get current contents."
                        )
                    file_idx += 1  # skip this line
                i += 1
            elif op_line.startswith('+'):
                # Add line
                added = op_line[1:]
                if not added.endswith('\n'):
                    added += '\n'
                new_lines.append(added)
                i += 1
            else:
                raise ValueError(
                    f"Invalid patch line in {file_path} at hunk line {i + 1}: "
                    f"'{op_line}'. Lines must start with ' ' (context), "
                    f"'+' (add), '-' (remove), or '@@' (anchor)."
                )

        # Add remaining lines after the hunk
        new_lines.extend(lines[file_idx:])
        lines = new_lines
        # Update search_start for any subsequent segments
        search_start = pos + sum(
            1 for op in ops if op.startswith(' ') or op.startswith('+')
        )

    return lines


def apply_patch(patch: str) -> dict:
    """Apply a text patch to one or more files.

    Format:
        *** Begin Patch
        *** Update File: path/to/file.py
        @@ anchor line
         context line (space prefix = must match existing line)
        -line to remove
        +line to add
         context line
        *** Add File: path/to/new_file.py
        +first line
        +second line
        *** Delete File: path/to/old_file.py
        *** End Patch

    Rules:
        - @@ finds the first matching line in the file (not a line number)
        - Space-prefixed lines are context that must match existing content
        - Use - to remove lines, + to add lines
        - Multiple @@ anchors narrow the search scope progressively
        - Read the file first to get accurate context and anchor lines

    Args:
        patch: Patch string in the format above

    Returns:
        Dictionary with success status and list of files modified
    """
    from pathlib import Path

    if not patch or not patch.strip():
        raise ValueError("Empty patch provided")

    lines = patch.split('\n')

    # Parse into file sections
    sections = []
    current_section = None
    current_lines = []

    for line in lines:
        if line.startswith('*** Begin Patch'):
            continue
        elif line.startswith('*** End Patch'):
            if current_section:
                current_section['lines'] = current_lines
                sections.append(current_section)
            break
        elif line.startswith('*** Update File: '):
            if current_section:
                current_section['lines'] = current_lines
                sections.append(current_section)
            path = line[len('*** Update File: '):]
            current_section = {'action': 'update', 'path': path}
            current_lines = []
        elif line.startswith('*** Add File: '):
            if current_section:
                current_section['lines'] = current_lines
                sections.append(current_section)
            path = line[len('*** Add File: '):]
            current_section = {'action': 'add', 'path': path}
            current_lines = []
        elif line.startswith('*** Delete File: '):
            if current_section:
                current_section['lines'] = current_lines
                sections.append(current_section)
            path = line[len('*** Delete File: '):]
            current_section = {'action': 'delete', 'path': path}
            current_lines = []
        else:
            current_lines.append(line)

    # Handle case where *** End Patch was missing
    if current_section and current_section not in sections:
        current_section['lines'] = current_lines
        sections.append(current_section)

    if not sections:
        raise ValueError("No file sections found in patch. Use *** Update File: / *** Add File: / *** Delete File: directives.")

    files_modified = []
    details = []

    for section in sections:
        file_path = section['path']

        # Security: prevent directory traversal
        if _is_traversal_path(file_path):
            raise ValueError(f"Invalid path: directory traversal not allowed: {file_path}")

        p = Path(file_path).resolve()

        if section['action'] == 'delete':
            if not p.exists():
                raise FileNotFoundError(f"Cannot delete: file not found: {file_path}")
            p.unlink()
            files_modified.append(file_path)
            details.append(f"Deleted {file_path}")

        elif section['action'] == 'add':
            if p.exists():
                raise ValueError(f"Cannot add: file already exists: {file_path}. Use *** Update File: instead.")
            # Create parent dirs if needed
            p.parent.mkdir(parents=True, exist_ok=True)
            content_lines = []
            for line in section['lines']:
                if line.startswith('+'):
                    content_lines.append(line[1:])
                elif line.strip():
                    # Non-empty lines without + prefix are unexpected in Add
                    content_lines.append(line)
            content = '\n'.join(content_lines)
            if content and not content.endswith('\n'):
                content += '\n'
            p.write_text(content, encoding='utf-8')
            files_modified.append(file_path)
            details.append(f"Created {file_path}")

        elif section['action'] == 'update':
            if not p.exists():
                raise FileNotFoundError(f"File not found: {file_path}")
            if not p.is_file():
                raise ValueError(f"Not a file: {file_path}")

            file_content = p.read_text(encoding='utf-8')
            file_lines = file_content.splitlines(keepends=True)
            # Ensure last line has newline
            if file_lines and not file_lines[-1].endswith('\n'):
                file_lines[-1] += '\n'

            hunk_lines = section['lines']
            # Remove empty trailing lines from hunk
            while hunk_lines and not hunk_lines[-1].strip():
                hunk_lines.pop()

            if hunk_lines:
                file_lines = _apply_hunk(file_lines, hunk_lines, file_path)

            result_content = ''.join(file_lines)
            p.write_text(result_content, encoding='utf-8')
            files_modified.append(file_path)
            details.append(f"Updated {file_path}")

    return {
        "success": True,
        "files_modified": files_modified,
        "details": details
    }
