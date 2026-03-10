"""Apply text patches to files using a unified-diff-like format.

Gate: file_write
Match: patch
"""


def _is_traversal_path(path: str) -> bool:
    """Check if path contains directory traversal attempts."""
    parts = path.replace('\\', '/').split('/')
    return '..' in parts


def _seek_sequence(lines, pattern, start=0):
    """Find the first contiguous occurrence of pattern lines within lines.

    Comparison strips trailing whitespace.  Returns the index of the first
    matching line, or -1 if not found.
    """
    if not pattern:
        return start
    plen = len(pattern)
    for i in range(start, len(lines) - plen + 1):
        ok = True
        for j in range(plen):
            if lines[i + j].rstrip('\n\r') != pattern[j].rstrip('\n\r'):
                ok = False
                break
        if ok:
            return i
    return -1


def _parse_chunks(hunk_lines):
    """Parse hunk lines into chunks, each with optional anchor + old/new lines.

    Follows the Codex apply_patch grammar:
      - @@ with text  -> chunk with change_context
      - @@ bare       -> chunk with change_context=None
      - ' ' prefix    -> context line (goes into both old_lines and new_lines)
      - '-' prefix    -> removed line (old_lines only)
      - '+' prefix    -> added line (new_lines only)
      - empty line    -> context (both old and new, as empty string)
    """
    chunks = []
    pending_anchor = None   # set when we see @@, consumed by next diff block
    first_chunk = True

    i = 0
    while i < len(hunk_lines):
        line = hunk_lines[i]

        # Check for @@ anchor
        if line.startswith('@@'):
            text = line[2:].strip()
            pending_anchor = text if text else None
            i += 1
            first_chunk = False
            continue

        # Must be a diff line (' ', '-', '+', or empty)
        old_lines = []
        new_lines = []
        while i < len(hunk_lines):
            dl = hunk_lines[i]
            if dl.startswith('@@'):
                break
            if dl == '':
                # Empty line = context
                old_lines.append('')
                new_lines.append('')
            elif dl[0] == ' ':
                old_lines.append(dl[1:])
                new_lines.append(dl[1:])
            elif dl[0] == '-':
                old_lines.append(dl[1:])
            elif dl[0] == '+':
                new_lines.append(dl[1:])
            else:
                if not old_lines and not new_lines and first_chunk:
                    # Tolerate missing @@ on first chunk (lenient)
                    old_lines.append(dl)
                    new_lines.append(dl)
                else:
                    break
            i += 1

        if old_lines or new_lines:
            chunks.append((pending_anchor, old_lines, new_lines))
            pending_anchor = None
            first_chunk = False

    return chunks


def _apply_hunk(lines, hunk_lines, file_path):
    """Apply a single hunk (set of changes with optional @@ anchors) to lines.

    Uses Codex-style block find-and-replace: for each chunk, find old_lines as
    a contiguous sequence in the file and replace with new_lines.
    """
    chunks = _parse_chunks(hunk_lines)
    if not chunks:
        return lines

    # Collect (start_idx, old_len, new_lines) replacements
    replacements = []
    line_index = 0

    for anchor, old, new in chunks:
        # If chunk has an anchor, seek to it first
        if anchor:
            anchor_stripped = anchor.strip()
            idx = -1
            for k in range(line_index, len(lines)):
                if lines[k].rstrip('\n\r').strip() == anchor_stripped:
                    idx = k
                    break
            if idx == -1:
                raise ValueError(
                    f"Anchor not found in {file_path}: '{anchor}'. "
                    f"The file may have changed since you last read it. "
                    f"Use read_file to get current contents."
                )
            line_index = idx + 1

        if not old:
            # Pure addition — insert at current position (or end of file)
            insert_at = len(lines) if line_index >= len(lines) else line_index
            replacements.append((insert_at, 0, new))
            continue

        # Find old_lines contiguously in the file starting from line_index
        found = _seek_sequence(lines, old, line_index)

        # Retry without trailing empty line (handles EOF edge case)
        if found == -1 and old and old[-1] == '':
            trimmed_old = old[:-1]
            trimmed_new = new[:-1] if new and new[-1] == '' else new
            found = _seek_sequence(lines, trimmed_old, line_index)
            if found != -1:
                old = trimmed_old
                new = trimmed_new

        if found == -1:
            snippet = '\n'.join(f'  {l}' for l in old[:5])
            raise ValueError(
                f"Failed to find expected lines in {file_path}:\n{snippet}\n"
                f"Use read_file to get current contents."
            )

        replacements.append((found, len(old), new))
        line_index = found + len(old)

    # Apply replacements in reverse order so indices stay valid
    for start_idx, old_len, new_segment in reversed(replacements):
        # Remove old lines
        del lines[start_idx:start_idx + old_len]
        # Insert new lines (with newline endings)
        for offset, nl in enumerate(new_segment):
            if not nl.endswith('\n'):
                nl += '\n'
            lines.insert(start_idx + offset, nl)

    return lines


def apply_patch(patch: str) -> dict:
    """Apply a patch to create, edit, or delete files. Preferred tool for editing existing code — finds and replaces specific blocks precisely. Always read the target file first to ensure context lines match. Include enough context lines for unambiguous matching.

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
