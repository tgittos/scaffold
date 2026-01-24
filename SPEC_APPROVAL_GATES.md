# Approval Gates Specification

Require user confirmation before executing potentially destructive operations.

## Goals

1. Prevent unintended side effects from LLM-initiated tool calls
2. Give users visibility into what operations are being performed
3. Allow power users to bypass gates for trusted patterns
4. Maintain conversational flow when approval is denied

## Terminology

- **Gate**: A checkpoint that pauses execution pending user approval
- **Category**: A classification of tools by risk level or operation type
- **Allowlist**: Patterns that bypass the gate for specific tool/argument combinations
- **Deny**: User rejects the operation; tool returns error to LLM
- **Allow**: User approves; execution proceeds normally
- **Allow Always**: User approves and adds pattern to session allowlist

## Platform Considerations

Ralph runs on Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD via Cosmopolitan. Approval gates must handle platform differences:

| Aspect | POSIX (Linux/macOS/BSD) | Windows |
|--------|-------------------------|---------|
| Path separator | `/` | `\` (normalized to `/`) |
| Case sensitivity | Case-sensitive | Case-insensitive |
| Symlink flags | `O_NOFOLLOW` | `FILE_FLAG_OPEN_REPARSE_POINT` |
| File identity | `st_ino` + `st_dev` | `nFileIndex*` + `dwVolumeSerialNumber` |
| Shell type | bash/sh/zsh | cmd.exe or PowerShell |
| Metacharacters | `; && \|\| \| $()` | `& && \|\| \| %VAR%` |

All paths are normalized to forward slashes internally. Protected file matching uses case-insensitive comparison on Windows. Shell parsing detects the active shell type and applies appropriate rules.

## Tool Categories

Tools are classified into categories that can be individually configured:

| Category | Tools | Default |
|----------|-------|---------|
| `file_write` | write_file, append_file, apply_delta | `gate` |
| `file_read` | read_file, file_info, list_dir, search_files | `allow` |
| `shell` | shell | `gate` |
| `network` | web_fetch | `gate` |
| `memory` | remember, recall_memories, forget_memory, vector_db_* | `allow` |
| `subagent` | subagent, subagent_status | `gate` |
| `mcp` | All mcp_* tools | `gate` |
| `python` | python (interpreter), dynamic tools | `allow` |

Default values:
- `gate`: Require approval before execution
- `allow`: Execute without prompting
- `deny`: Block entirely (never execute)

### Memory Tools and Secrets

Memory tools default to `allow` because they don't perform external I/O. However, the system prompt MUST include instructions preventing the LLM from storing secrets:

```
NEVER use remember, vector_db_add, or similar memory tools to store:
- API keys, tokens, or credentials
- Passwords or private keys
- Environment variable values from .env files
- Any data the user identifies as sensitive

If asked to remember such information, decline and explain why.
```

This relies on LLM instruction-following rather than technical enforcement, which is appropriate for this threat model.

## Configuration

**Precedence**: CLI flags override config file settings.

### Config File (ralph.config.json)

```json
{
  "approval_gates": {
    "enabled": true,
    "categories": {
      "file_write": "gate",
      "file_read": "allow",
      "shell": "gate",
      "network": "gate",
      "memory": "allow",
      "subagent": "gate",
      "mcp": "gate",
      "python": "allow"
    },
    "allowlist": [
      {"tool": "shell", "command": ["ls"]},
      {"tool": "shell", "command": ["cat"]},
      {"tool": "shell", "command": ["pwd"]},
      {"tool": "shell", "command": ["git", "status"]},
      {"tool": "shell", "command": ["git", "log"]},
      {"tool": "shell", "command": ["git", "diff"]},
      {"tool": "shell", "command": ["git", "branch"]},
      {"tool": "shell", "command": ["dir"], "shell": "cmd"},
      {"tool": "shell", "command": ["Get-ChildItem"], "shell": "powershell"},
      {"tool": "write_file", "pattern": "\\.test\\.c$"}
    ]
  }
}
```

**Note**: The config file itself (`ralph.config.json`) is protected and cannot be modified by any tool. See [Protected Files](#protected-files).

### Command-Line Flags

```bash
ralph --yolo                     # Disable all approval gates for session
ralph --allow "shell:git,status" # Add shell command to session allowlist
```

## Protected Files

The following files are **hard-blocked** from modification by any tool, regardless of gate configuration or allowlist:

- `ralph.config.json` (and any path matching `**/ralph.config.json`)
- `.ralph/config.json`
- `.env` files (`**/.env`, `**/.env.*`)

Attempts to write, append, or modify these files return an error:

```json
{
  "error": "protected_file",
  "message": "Cannot modify protected configuration file",
  "path": "ralph.config.json"
}
```

This protection is enforced at the tool execution layer, not the gate layer, and cannot be bypassed.

## Shell Command Matching

Shell commands use **parsed command matching**, not regex patterns. This prevents command injection attacks that exploit pattern prefix matching.

### Command Parsing

When a shell command is submitted for gate checking:

1. **Parse** the command string into tokens using shell parsing rules
2. **Extract** the base command and initial arguments
3. **Check** for command chaining operators (`;`, `&&`, `||`, `|`)
4. **Match** against allowlist entries

### Allowlist Entry Format

```json
{
  "tool": "shell",
  "command": ["git", "status"]
}
```

The `command` array specifies the exact command prefix that must match:
- `["git", "status"]` matches `git status`, `git status -s`, `git status --porcelain`
- `["git", "status"]` does NOT match `git status; rm -rf /`
- `["ls"]` matches `ls`, `ls -la`, `ls /tmp`
- `["ls"]` does NOT match `ls; cat /etc/passwd`

### Command Chain Detection

Commands containing chain operators are **never** matched by allowlist entries:

```bash
git status                    # Matches ["git", "status"] ✓
git status && echo done       # No match, requires approval ✗
git status; rm -rf /          # No match, requires approval ✗
ls | grep foo                 # No match (pipe), requires approval ✗
$(cat /etc/passwd)            # No match (subshell), requires approval ✗
`cat /etc/passwd`             # No match (backticks), requires approval ✗
```

### Dangerous Pattern Detection

The following patterns always require approval regardless of allowlist:

| Pattern | Reason |
|---------|--------|
| `rm -rf`, `rm -fr` | Recursive forced deletion |
| `> /dev/sd*` | Direct disk write |
| `dd if=* of=/dev/*` | Disk overwrite |
| `chmod 777`, `chmod -R` | Broad permission changes |
| `curl * \| *sh`, `wget * \| *sh` | Remote code execution |
| `:(){:\|:&};:` | Fork bomb |

These are checked against the raw command string before parsing.

### Parser Security Requirements

The shell parser must be conservative. Commands containing any of the following are **never** matched by allowlist and always require approval:

| Pattern | Reason |
|---------|--------|
| ANSI-C quoting (`$'...'`) | Can encode semicolons, pipes, etc. |
| Non-ASCII characters | Unicode lookalikes for operators |
| Unbalanced quotes | Parsing ambiguity |
| Null bytes | Truncation attacks |
| Escape sequences at token boundaries | `git\ status; rm` could confuse parsers |

The parser should use simple tokenization rules:
1. Split on unquoted whitespace
2. Respect double and single quotes (no escape interpretation inside singles)
3. Detect metacharacters: `; | & ( ) $ \` > < `
4. If any metacharacter appears unquoted, mark command as having chains/pipes/subshells

Do **not** attempt to fully emulate bash parsing. When in doubt, require approval.

## Cross-Platform Shell Parsing

Shell command parsing must account for platform differences. Ralph runs on Windows, Linux, macOS, and BSD via Cosmopolitan.

### Shell Detection

Detect the active shell at runtime:

```c
typedef enum {
    SHELL_TYPE_POSIX,       // bash, sh, zsh, dash
    SHELL_TYPE_CMD,         // Windows cmd.exe
    SHELL_TYPE_POWERSHELL,  // PowerShell (Windows or Core)
    SHELL_TYPE_UNKNOWN
} ShellType;

ShellType detect_shell_type(void) {
#ifdef _WIN32
    // Check COMSPEC and PSModulePath
    const char *comspec = getenv("COMSPEC");
    const char *psmodule = getenv("PSModulePath");

    if (psmodule && strlen(psmodule) > 0) {
        return SHELL_TYPE_POWERSHELL;
    }
    if (comspec && strcasestr(comspec, "cmd.exe")) {
        return SHELL_TYPE_CMD;
    }
    return SHELL_TYPE_CMD;  // Default on Windows
#else
    const char *shell = getenv("SHELL");
    if (shell) {
        if (strstr(shell, "pwsh") || strstr(shell, "powershell")) {
            return SHELL_TYPE_POWERSHELL;
        }
    }
    return SHELL_TYPE_POSIX;
#endif
}
```

### Platform-Specific Metacharacters

| Shell | Chain Operators | Pipe | Subshell | Redirect |
|-------|-----------------|------|----------|----------|
| POSIX | `; && \|\|` | `\|` | `$() \`\`` | `> >> < <<` |
| cmd.exe | `& && \|\|` | `\|` | N/A | `> >> <` |
| PowerShell | `; && \|\|` | `\|` | `$()` | `> >> <` |

**Note**: cmd.exe uses `&` as the unconditional command separator (equivalent to `;` in bash).

### cmd.exe Parsing Rules

```c
typedef struct {
    char **tokens;
    int token_count;
    int has_chain;      // Contains &, &&, ||
    int has_pipe;
    int has_redirect;
    int has_percent;    // %VAR% expansion
    int has_caret;      // ^ escape character
} ParsedCmdCommand;

ParsedCmdCommand *parse_cmd_command(const char *command) {
    ParsedCmdCommand *cmd = calloc(1, sizeof(*cmd));

    // cmd.exe metacharacters that always require approval
    const char *dangerous_chars = "&|<>^%";

    for (const char *p = command; *p; p++) {
        // Check for unquoted metacharacters
        if (strchr(dangerous_chars, *p)) {
            // In cmd.exe, only double quotes are string delimiters
            // Check if we're inside quotes
            if (!in_double_quotes(command, p - command)) {
                if (*p == '&') cmd->has_chain = 1;
                if (*p == '|') cmd->has_pipe = 1;
                if (*p == '<' || *p == '>') cmd->has_redirect = 1;
                if (*p == '%') cmd->has_percent = 1;
                if (*p == '^') cmd->has_caret = 1;
            }
        }
    }

    // Tokenize on whitespace outside quotes
    cmd->tokens = tokenize_cmd(command, &cmd->token_count);
    return cmd;
}
```

### PowerShell Parsing Rules

PowerShell has additional complexity:

```c
typedef struct {
    char **tokens;
    int token_count;
    int has_chain;          // ; && ||
    int has_pipe;           // |
    int has_subexpression;  // $()
    int has_scriptblock;    // { }
    int has_redirect;
    int has_variable;       // $var
    int has_invoke;         // & (call operator) or . (dot-source)
} ParsedPowerShellCommand;

// PowerShell-specific dangerous patterns
static const char *PS_DANGEROUS_PATTERNS[] = {
    "Invoke-Expression",    // iex - arbitrary code execution
    "Invoke-Command",       // icm - remote execution
    "Start-Process",        // spawns new process
    "Invoke-WebRequest",    // network access
    "Invoke-RestMethod",    // network access
    "iex",                  // alias
    "icm",                  // alias
    "iwr",                  // alias
    "irm",                  // alias
    "-EncodedCommand",      // base64 encoded script
    "-enc",                 // short form
    "DownloadString",       // .NET web access
    "DownloadFile",         // .NET web access
    NULL
};

int powershell_command_is_dangerous(const char *command) {
    // Case-insensitive check for dangerous cmdlets
    char *lower = strdup(command);
    for (char *p = lower; *p; p++) *p = tolower(*p);

    for (int i = 0; PS_DANGEROUS_PATTERNS[i]; i++) {
        char *pattern_lower = strdup(PS_DANGEROUS_PATTERNS[i]);
        for (char *p = pattern_lower; *p; p++) *p = tolower(*p);

        if (strstr(lower, pattern_lower)) {
            free(pattern_lower);
            free(lower);
            return 1;
        }
        free(pattern_lower);
    }
    free(lower);
    return 0;
}
```

### Unified Parser Interface

```c
typedef struct {
    char **tokens;
    int token_count;
    int has_chain;
    int has_pipe;
    int has_subshell;
    int has_redirect;
    int is_dangerous;
    ShellType shell_type;
} ParsedShellCommand;

ParsedShellCommand *parse_shell_command(const char *command) {
    ShellType type = detect_shell_type();
    ParsedShellCommand *result = calloc(1, sizeof(*result));
    result->shell_type = type;

    switch (type) {
        case SHELL_TYPE_CMD:
            // Parse using cmd.exe rules
            parse_cmd_into(command, result);
            break;

        case SHELL_TYPE_POWERSHELL:
            // Parse using PowerShell rules
            parse_powershell_into(command, result);
            result->is_dangerous = powershell_command_is_dangerous(command);
            break;

        case SHELL_TYPE_POSIX:
        default:
            // Parse using POSIX rules
            parse_posix_into(command, result);
            result->is_dangerous = shell_command_is_dangerous(command);
            break;
    }

    return result;
}
```

### Allowlist Matching Per Shell

Allowlist entries can optionally specify shell type:

```json
{
  "allowlist": [
    {"tool": "shell", "command": ["git", "status"]},
    {"tool": "shell", "command": ["dir"], "shell": "cmd"},
    {"tool": "shell", "command": ["Get-ChildItem"], "shell": "powershell"},
    {"tool": "shell", "command": ["ls"], "shell": "posix"}
  ]
}
```

When `shell` is omitted, the entry matches any shell type. When specified, it only matches that shell.

### Safe Command Equivalents

Some commands have cross-platform equivalents that should be treated identically:

| POSIX | cmd.exe | PowerShell | Purpose |
|-------|---------|------------|---------|
| `ls` | `dir` | `Get-ChildItem`, `gci`, `ls` | List directory |
| `cat` | `type` | `Get-Content`, `gc`, `cat` | Read file |
| `pwd` | `cd` (no args) | `Get-Location`, `gl`, `pwd` | Current directory |
| `rm` | `del`, `erase` | `Remove-Item`, `ri`, `rm` | Delete file |
| `cp` | `copy` | `Copy-Item`, `cp` | Copy file |
| `mv` | `move`, `ren` | `Move-Item`, `mv` | Move/rename file |

The allowlist should recognize these as equivalent when appropriate:

```c
int commands_are_equivalent(
    const char *allowed_cmd,
    const char *actual_cmd,
    ShellType allowed_shell,
    ShellType actual_shell
) {
    // Exact match
    if (strcmp(allowed_cmd, actual_cmd) == 0) return 1;

    // Cross-platform equivalents
    static const char *equivalents[][4] = {
        {"ls", "dir", "Get-ChildItem", "gci"},
        {"cat", "type", "Get-Content", "gc"},
        {"rm", "del", "Remove-Item", "ri"},
        // ... more equivalents
        {NULL}
    };

    for (int i = 0; equivalents[i][0]; i++) {
        int allowed_found = 0, actual_found = 0;
        for (int j = 0; j < 4 && equivalents[i][j]; j++) {
            if (strcasecmp(allowed_cmd, equivalents[i][j]) == 0) allowed_found = 1;
            if (strcasecmp(actual_cmd, equivalents[i][j]) == 0) actual_found = 1;
        }
        if (allowed_found && actual_found) return 1;
    }

    return 0;
}
```

## Dynamic Tools (Python File Tools)

Python-defined tools in `src/tools/python_defaults/` use docstrings for their interface.

### Security Model

Dynamic tools default to **allow** (no gate). The security boundary is at file creation:

1. Agent writes a new tool file → `file_write` gate triggers
2. User reviews the tool code and approves/denies the write
3. Once written, the tool is trusted and executes without per-call approval

This is the correct trust model: if you approve writing a tool, you're saying it's safe to use. Pattern matching on tool arguments is not supported for dynamic tools—the code itself is the security-relevant artifact, not the arguments passed to it.

For file-based Python tools (read_file, write_file, etc.), these are categorized by their operation type, not as `python`. See [Built-in Tool Overrides](#built-in-tool-overrides).

### Gate Metadata

Tools can declare explicit gate info via a `Gate:` directive in the module docstring:

```python
"""Read file contents with optional line range.

Gate: file_read
Match: path
"""

def read_file(path: str, start_line: int = 0, end_line: int = 0) -> str:
    ...
```

| Directive | Description |
|-----------|-------------|
| `Gate: <category>` | Override default category |
| `Match: <arg_name>` | Extract named argument for pattern matching |

### Built-in Tool Overrides

Core dynamic tools ship with explicit metadata:

| Tool | Gate | Match |
|------|------|-------|
| `read_file` | `file_read` | `path` |
| `write_file` | `file_write` | `path` |
| `append_file` | `file_write` | `path` |
| `list_dir` | `file_read` | `path` |
| `search_files` | `file_read` | `pattern` |
| `shell` | `shell` | (parsed command) |
| `web_fetch` | `network` | `url` |

## Allowlist Pattern Syntax

Patterns use POSIX extended regex matching against tool arguments.

```json
{
  "tool": "write_file",
  "pattern": "^\\.?/src/.*\\.c$"
}
```

**Note**: Wildcard tool matching (`"tool": "*"`) is not supported. Each allowlist entry must specify an exact tool name.

**Matching rules:**

1. `tool` must match exactly (no wildcards)
2. `pattern` is matched against the tool's **match target**:
   - Shell: Uses parsed command matching (see above)
   - Other tools: regex against specified argument
   - Dynamic tools (python category): No pattern matching, default allow

**C Native Tool Match Targets:**

| Tool | Category | Match Target |
|------|----------|--------------|
| `remember` | `memory` | `information` argument |
| `recall_memories` | `memory` | `query` argument |
| `forget_memory` | `memory` | `memory_id` argument |
| `process_pdf_document` | `file_read` | `file_path` argument |
| `subagent` | `subagent` | `task` argument |
| `subagent_status` | `subagent` | `subagent_id` argument |
| `todo` | `memory` | full JSON arguments |
| `vector_db_*` | `memory` | full JSON arguments |
| `mcp_*` | `mcp` | full JSON arguments |

## Path Resolution and TOCTOU Protection

To prevent time-of-check-to-time-of-use attacks, path-based operations use atomic filesystem operations rather than simple `realpath()` checks.

### Threat Model

TOCTOU attacks exploit the gap between checking a path and using it:
1. User approves write to `./safe.txt`
2. Attacker replaces `./safe.txt` with symlink to `/etc/passwd`
3. Write executes against the symlink target

Simple `realpath()` + `stat()` checks are insufficient because the filesystem can change between the check and the operation.

### Atomic File Operations

#### For Existing Files (Overwrite)

```c
// 1. Open with O_NOFOLLOW to reject symlinks at the final component
int fd = open(path, O_RDWR | O_NOFOLLOW);
if (fd < 0 && errno == ELOOP) {
    // Final component is a symlink - reject
    return error_symlink_not_allowed();
}

// 2. Verify inode matches what was approved
struct stat st;
if (fstat(fd, &st) < 0) {
    close(fd);
    return error_stat_failed();
}

if (st.st_ino != approved->inode || st.st_dev != approved->device) {
    close(fd);
    return error_path_changed();
}

// 3. Perform write using the fd (not the path)
write(fd, data, len);
close(fd);
```

#### For New Files (Creation)

```c
// 1. Resolve and verify parent directory
char *parent = dirname(strdup(path));
int parent_fd = open(parent, O_RDONLY | O_DIRECTORY);
if (parent_fd < 0) {
    return error_parent_not_found();
}

// 2. Verify parent inode matches approval
struct stat parent_st;
if (fstat(parent_fd, &parent_st) < 0 ||
    parent_st.st_ino != approved->parent_inode ||
    parent_st.st_dev != approved->parent_device) {
    close(parent_fd);
    return error_parent_changed();
}

// 3. Create file atomically with O_EXCL (fails if exists)
const char *basename = get_basename(path);
int fd = openat(parent_fd, basename, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
if (fd < 0) {
    close(parent_fd);
    if (errno == EEXIST) {
        return error_file_already_exists();
    }
    return error_create_failed();
}

// 4. Write content
write(fd, data, len);
close(fd);
close(parent_fd);
```

#### For Appends

```c
// Same as overwrite, but use O_APPEND
int fd = open(path, O_WRONLY | O_APPEND | O_NOFOLLOW);
// ... verify inode/device with fstat ...
```

### Platform-Specific Considerations

#### POSIX (Linux, macOS, BSD)

| Flag | Purpose |
|------|---------|
| `O_NOFOLLOW` | Reject symlinks at final path component |
| `O_EXCL` | Fail if file exists (atomic create) |
| `O_DIRECTORY` | Ensure target is a directory |
| `openat()` | Open relative to directory fd (avoids races on parent path) |

**macOS Caveat**: `O_NOFOLLOW` on macOS only applies to the final component. Symlinks in parent directories are still followed. Use `openat()` with a verified parent fd.

#### Windows

Windows lacks `O_NOFOLLOW` but provides equivalent functionality:

```c
#ifdef _WIN32
// Use FILE_FLAG_OPEN_REPARSE_POINT to detect symlinks/junctions
HANDLE h = CreateFileW(
    path,
    GENERIC_WRITE,
    0,  // No sharing during operation
    NULL,
    CREATE_NEW,  // Equivalent to O_EXCL
    FILE_FLAG_OPEN_REPARSE_POINT,  // Don't follow reparse points
    NULL
);

// Check if it's a reparse point (symlink/junction)
BY_HANDLE_FILE_INFORMATION info;
GetFileInformationByHandle(h, &info);
if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    CloseHandle(h);
    return error_symlink_not_allowed();
}
#endif
```

**Windows File IDs**: Use `GetFileInformationByHandle()` to get `nFileIndexHigh`/`nFileIndexLow` as the equivalent of inode, and `dwVolumeSerialNumber` as device.

#### Network Filesystems (NFS, CIFS/SMB)

Network filesystems have weaker guarantees:

| Issue | Mitigation |
|-------|------------|
| Inode reuse | Accept as limitation; document in user-facing error |
| No `O_NOFOLLOW` support | Fall back to `lstat()` + `open()` + `fstat()` comparison |
| Stale handles | Set short timeouts; retry with fresh open |
| Attribute caching | Use `O_DIRECT` where available to bypass cache |

**Conservative Mode**: When filesystem type is detected as network-mounted (check `/proc/mounts` on Linux, `GetVolumeInformation()` on Windows), require explicit user confirmation:

```
⚠ Network filesystem detected (NFS)
Path verification may be unreliable. Proceed anyway? [y/n]
```

### ApprovedPath Structure (Updated)

```c
typedef struct {
    char *user_path;           // Original path from tool call
    char *resolved_path;       // Canonical path at approval time

    // For existing files
    ino_t inode;               // Inode at approval (0 if new file)
    dev_t device;              // Device at approval

    // For new files - parent directory verification
    ino_t parent_inode;        // Parent dir inode
    dev_t parent_device;       // Parent dir device
    char *parent_path;         // Resolved parent path

    int existed;               // File existed at approval time
    int is_network_fs;         // Detected as NFS/CIFS/etc

#ifdef _WIN32
    DWORD volume_serial;       // Windows volume serial number
    DWORD index_high;          // Windows file index (high)
    DWORD index_low;           // Windows file index (low)
#endif
} ApprovedPath;
```

### Verification Flow

```c
int verify_and_open_approved_path(
    const ApprovedPath *approved,
    int flags,  // O_RDONLY, O_WRONLY, etc.
    int *out_fd
) {
    if (approved->existed) {
        // Existing file: open and verify inode
        int fd = open(approved->resolved_path, flags | O_NOFOLLOW);
        if (fd < 0) {
            if (errno == ELOOP) return VERIFY_ERR_SYMLINK;
            if (errno == ENOENT) return VERIFY_ERR_DELETED;
            return VERIFY_ERR_OPEN;
        }

        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            return VERIFY_ERR_STAT;
        }

        if (st.st_ino != approved->inode ||
            st.st_dev != approved->device) {
            close(fd);
            return VERIFY_ERR_INODE_MISMATCH;
        }

        *out_fd = fd;
        return VERIFY_OK;

    } else {
        // New file: verify parent, create with O_EXCL
        int parent_fd = open(approved->parent_path, O_RDONLY | O_DIRECTORY);
        if (parent_fd < 0) return VERIFY_ERR_PARENT;

        struct stat parent_st;
        if (fstat(parent_fd, &parent_st) < 0 ||
            parent_st.st_ino != approved->parent_inode ||
            parent_st.st_dev != approved->parent_device) {
            close(parent_fd);
            return VERIFY_ERR_PARENT_CHANGED;
        }

        const char *base = strrchr(approved->user_path, '/');
        base = base ? base + 1 : approved->user_path;

        int fd = openat(parent_fd, base,
                        flags | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
        close(parent_fd);

        if (fd < 0) {
            if (errno == EEXIST) return VERIFY_ERR_ALREADY_EXISTS;
            return VERIFY_ERR_CREATE;
        }

        *out_fd = fd;
        return VERIFY_OK;
    }
}
```

## Allow Always Pattern Generation

When user selects "Allow always", patterns are generated to match similar future operations while minimizing scope.

### Strategy (Following Claude Code Conventions)

**For file paths:**
- Extract the directory component
- Generate pattern matching that directory and similar file extensions
- Never generate patterns matching parent directories

| Path | Generated Pattern |
|------|-------------------|
| `./src/foo/bar.c` | `^\./src/foo/.*\.c$` |
| `./test/test_gates.c` | `^\./test/test_.*\.c$` |
| `/tmp/scratch.txt` | `^/tmp/scratch\.txt$` (exact match for /tmp) |
| `./README.md` | `^\./README\.md$` (exact match for root files) |

**For shell commands:**
- Add the base command and first argument to allowlist
- Never auto-allow commands with pipes, chains, or redirects

| Command | Generated Entry |
|---------|-----------------|
| `git commit -m "msg"` | `{"tool": "shell", "command": ["git", "commit"]}` |
| `make test` | `{"tool": "shell", "command": ["make", "test"]}` |
| `npm install lodash` | `{"tool": "shell", "command": ["npm", "install"]}` |

**For network:**
- Parse URL and extract scheme + hostname
- Generate pattern that matches scheme, exact hostname, and requires path separator
- `https://api.example.com/v1/users` → `^https://api\.example\.com(/|$)`

This prevents matching attacker-controlled subdomains like `api.example.com.evil.com`.


### User Confirmation for Broad Patterns

If a generated pattern would match more than the current operation, show a confirmation:

```
┌─ Pattern Confirmation ───────────────────────────────────────┐
│                                                              │
│  This will allow future operations matching:                 │
│  Tool: write_file                                            │
│  Pattern: ^\.\/src\/foo\/.*\.c$                              │
│                                                              │
│  Example matches:                                            │
│    ./src/foo/bar.c ✓                                         │
│    ./src/foo/baz.c ✓                                         │
│    ./src/foo/other.c ✓                                       │
│                                                              │
│  [y] Confirm  [e] Edit pattern  [x] Exact match only         │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## User Prompt Interface

When a gate triggers, display:

```
┌─ Approval Required ──────────────────────────────────────────┐
│                                                              │
│  Tool: shell                                                 │
│  Command: rm -rf ./build                                     │
│                                                              │
│  [y] Allow  [n] Deny  [a] Allow always  [?] Details          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

**Options:**

| Key | Action | Effect |
|-----|--------|--------|
| `y` | Allow | Execute this call only |
| `n` | Deny | Return error to LLM, continue conversation |
| `a` | Allow always | Generate pattern, confirm scope, add to session allowlist |
| `?` | Details | Show full arguments JSON and resolved paths |
| `Ctrl+C` | Abort | Return to conversation without executing |

### Batch Approval

When multiple tool calls are pending:

```
┌─ Approval Required (3 operations) ────────────────────────────┐
│                                                               │
│  1. shell: mkdir -p ./src/gates                               │
│  2. write_file: ./src/gates/approval.h                        │
│  3. write_file: ./src/gates/approval.c                        │
│                                                               │
│  [y] Allow all  [n] Deny all  [1-3] Review individual         │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

## Denial Rate Limiting

To prevent approval fatigue attacks (repeated requests hoping for accidental approval), implement exponential backoff on denials.

### Tracking

```c
typedef struct {
    char *tool;
    char *category;
    int denial_count;
    time_t last_denial;
    time_t backoff_until;
} DenialTracker;
```

### Backoff Schedule

| Consecutive Denials | Backoff Period |
|--------------------|----------------|
| 1-2 | None (immediate retry allowed) |
| 3 | 5 seconds |
| 4 | 15 seconds |
| 5 | 60 seconds |
| 6+ | 5 minutes |

### Behavior During Backoff

Tool calls during backoff period are automatically denied with a message:

```json
{
  "error": "rate_limited",
  "message": "Too many denied requests for shell tool. Wait 60 seconds before retrying.",
  "retry_after": 60,
  "tool": "shell"
}
```

The backoff counter resets when:
- User approves a call for that tool/category
- Session is restarted
- Backoff period expires without new denial

## Subagent Behavior

Subagents inherit gate configuration from their parent, with restrictions.

### Gate Inheritance

When a subagent is spawned, it receives:
1. Parent's category configuration
2. Parent's static allowlist (from config file)
3. **Not** the parent's session allowlist (runtime "allow always" entries)

```c
// In subagent spawn logic
SubagentConfig config = {
    .gate_config = clone_gate_config(parent->gate_config),
    .inherit_session_allowlist = false,
};
```

### Subagent Approval Scope

The subagent spawn approval prompt includes the task description. This approval covers:
- Spawning the subagent process
- **Not** individual tool calls within the subagent

The subagent will prompt the user for approvals just like the parent process.

### Subagent TTY Handling

Subagents need access to the TTY for approval prompts:
- Subagent inherits parent's stdin/stdout/stderr
- Parent blocks on subagent I/O during approval prompts
- If no TTY available, subagent runs with `deny` default for gated categories

### Subagent Deadlock Prevention

When parent and subagent both require TTY access for approval prompts, deadlock can occur:

1. Parent spawns subagent, waits for completion
2. Subagent triggers a gated operation, waits for user input
3. Parent is blocked, can't process user input
4. Deadlock

#### Architecture: Approval Proxy

The parent maintains exclusive TTY ownership and proxies approval requests from subagents via IPC:

```c
typedef struct {
    int request_fd;      // Subagent writes requests here
    int response_fd;     // Parent writes responses here
    pid_t subagent_pid;
} ApprovalChannel;

typedef struct {
    char *tool_name;
    char *arguments_json;
    char *display_summary;
    uint32_t request_id;
} ApprovalRequest;

typedef struct {
    uint32_t request_id;
    ApprovalResult result;
    char *pattern;        // If ALLOWED_ALWAYS, the generated pattern
} ApprovalResponse;
```

#### Subagent Side

```c
// In subagent: instead of prompting TTY directly, send request to parent
ApprovalResult subagent_request_approval(
    const ApprovalChannel *channel,
    const ToolCall *tool_call,
    ApprovedPath *out_path
) {
    ApprovalRequest req = {
        .tool_name = tool_call->name,
        .arguments_json = tool_call->arguments,
        .display_summary = format_tool_summary(tool_call),
        .request_id = next_request_id()
    };

    // Serialize and send to parent
    char *serialized = serialize_approval_request(&req);
    write(channel->request_fd, serialized, strlen(serialized) + 1);
    free(serialized);

    // Block waiting for parent response (with timeout)
    ApprovalResponse resp;
    if (!read_approval_response(channel->response_fd, &resp, APPROVAL_TIMEOUT_MS)) {
        return APPROVAL_DENIED;  // Timeout = deny
    }

    if (resp.result == APPROVAL_ALLOWED_ALWAYS && resp.pattern) {
        // Parent already added to session allowlist; we just proceed
    }

    return resp.result;
}
```

#### Parent Side

```c
// Parent approval loop: multiplexes between own operations and subagent requests
void parent_approval_loop(
    ApprovalGateConfig *config,
    ApprovalChannel *channels,
    int channel_count
) {
    fd_set read_fds;
    int max_fd = STDIN_FILENO;

    // Build fd set for all subagent request channels
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    for (int i = 0; i < channel_count; i++) {
        FD_SET(channels[i].request_fd, &read_fds);
        if (channels[i].request_fd > max_fd) {
            max_fd = channels[i].request_fd;
        }
    }

    struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};  // 100ms poll
    int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (ready > 0) {
        // Check for subagent approval requests
        for (int i = 0; i < channel_count; i++) {
            if (FD_ISSET(channels[i].request_fd, &read_fds)) {
                handle_subagent_approval_request(config, &channels[i]);
            }
        }
    }
}

void handle_subagent_approval_request(
    ApprovalGateConfig *config,
    ApprovalChannel *channel
) {
    ApprovalRequest req;
    if (!read_approval_request(channel->request_fd, &req)) {
        return;
    }

    // Display prompt to user (parent owns TTY)
    printf("\n┌─ Subagent Approval Required ─────────────────────────────────┐\n");
    printf("│  PID: %d                                                      │\n", channel->subagent_pid);
    printf("│  Tool: %s\n", req.tool_name);
    printf("│  %s\n", req.display_summary);
    printf("│                                                               │\n");
    printf("│  [y] Allow  [n] Deny  [a] Allow always  [?] Details           │\n");
    printf("└───────────────────────────────────────────────────────────────┘\n");

    // Get user response
    ApprovalResult result = get_user_approval_choice();

    ApprovalResponse resp = {
        .request_id = req.request_id,
        .result = result,
        .pattern = NULL
    };

    if (result == APPROVAL_ALLOWED_ALWAYS) {
        // Generate pattern and add to parent's session allowlist
        resp.pattern = generate_allowlist_pattern(req.tool_name, req.arguments_json);
        approval_gate_add_allowlist(config, req.tool_name, resp.pattern);
    }

    // Send response back to subagent
    char *serialized = serialize_approval_response(&resp);
    write(channel->response_fd, serialized, strlen(serialized) + 1);
    free(serialized);
    free(resp.pattern);
}
```

#### Timeout Handling

```c
#define APPROVAL_TIMEOUT_MS 300000  // 5 minutes

// If parent doesn't respond within timeout, deny the operation
// This prevents indefinite hangs if parent crashes or is killed

int read_approval_response(int fd, ApprovalResponse *resp, int timeout_ms) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN};

    int ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0) {
        return 0;  // Timeout or error
    }

    // Read and deserialize response
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;

    buf[n] = '\0';
    return deserialize_approval_response(buf, resp);
}
```

#### Spawn with Channels

```c
typedef struct {
    pid_t pid;
    ApprovalChannel channel;
    int status;              // Running, completed, failed
} SubagentHandle;

SubagentHandle *spawn_subagent_with_approval(
    const char *task,
    const ApprovalGateConfig *parent_config
) {
    int request_pipe[2], response_pipe[2];
    if (pipe(request_pipe) < 0 || pipe(response_pipe) < 0) {
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(request_pipe[0]); close(request_pipe[1]);
        close(response_pipe[0]); close(response_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        // Child (subagent)
        close(request_pipe[0]);   // Close read end of request pipe
        close(response_pipe[1]);  // Close write end of response pipe

        ApprovalChannel channel = {
            .request_fd = request_pipe[1],
            .response_fd = response_pipe[0],
            .subagent_pid = getpid()
        };

        ApprovalGateConfig child_config;
        approval_gate_init_from_parent(&child_config, parent_config);
        child_config.approval_channel = &channel;  // Use proxy instead of TTY

        run_subagent(task, &child_config);
        exit(0);
    }

    // Parent
    close(request_pipe[1]);   // Close write end of request pipe
    close(response_pipe[0]);  // Close read end of response pipe

    SubagentHandle *handle = calloc(1, sizeof(*handle));
    handle->pid = pid;
    handle->channel.request_fd = request_pipe[0];
    handle->channel.response_fd = response_pipe[1];
    handle->channel.subagent_pid = pid;
    handle->status = SUBAGENT_RUNNING;

    return handle;
}
```

#### Nested Subagents

When a subagent spawns its own subagent:
1. The child subagent sends approval requests to its parent (the first subagent)
2. The first subagent forwards the request up to the root parent
3. Response flows back down the chain

```c
// In subagent: check if we're a subagent ourselves
ApprovalResult request_approval(
    ApprovalGateConfig *config,
    const ToolCall *tool_call,
    ApprovedPath *out_path
) {
    if (config->approval_channel) {
        // We're a subagent: proxy to parent
        return subagent_request_approval(config->approval_channel, tool_call, out_path);
    } else {
        // We're the root: prompt TTY directly
        return approval_gate_prompt(config, tool_call, out_path);
    }
}
```

**Note**: The full implementation uses `spawn_subagent_with_approval()` shown in the "Spawn with Channels" section above, which sets up the IPC pipes for approval proxying.

### Rationale

Previous design ran subagents with `--yolo`, creating a privilege escalation path. Now:
- Users see and approve each operation, even in subagents
- No "trust the task description" assumption
- Subagents can't bypass parent restrictions
- No deadlock due to approval proxy architecture

## Non-Interactive Mode

One-shot mode and REPL mode use identical gate configuration by default. The `--yolo` flag is an explicit override, not a mode difference.

### Configuration Loading

Both modes load gates from:
1. Config file (`ralph.config.json`)
2. CLI flags

```bash
# REPL mode - gates enabled by default
ralph

# One-shot mode - same gates as REPL
echo "delete all test files" | ralph

# Explicit yolo override (either mode)
ralph --yolo
echo "delete all test files" | ralph --yolo
```

### Non-TTY Behavior

When stdin is not a TTY:
- Gated operations cannot prompt for approval
- Default action for gated categories: `deny`
- Allowed categories proceed normally
- Denied operations return structured error to LLM

```bash
# This will deny file writes and shell commands
echo "write a file" | ralph
# LLM receives: {"error": "non_interactive_gate", "message": "..."}

# Use --yolo to explicitly trust piped input
echo "write a file" | ralph --yolo
```

### Partial Trust

For scripts that need specific permissions:

```bash
# Allow specific categories
ralph --allow-category=file_write --allow-category=shell < script.txt

# Allow specific commands
ralph --allow="shell:make,test" --allow="shell:git,push" < deploy.txt
```

## Error Messages

When operation is denied, return structured error to LLM:

```json
{
  "error": "operation_denied",
  "message": "User denied permission to execute shell command",
  "tool": "shell",
  "suggestion": "Ask the user to perform this operation manually, or request permission with explanation"
}
```

This allows the LLM to adapt its approach rather than repeatedly attempting denied operations.

## Implementation

### Data Structures

```c
// src/core/approval_gate.h

typedef enum {
    GATE_ACTION_ALLOW,      // Execute without prompting
    GATE_ACTION_GATE,       // Require approval
    GATE_ACTION_DENY        // Never execute
} GateAction;

typedef enum {
    GATE_CATEGORY_FILE_WRITE,
    GATE_CATEGORY_FILE_READ,
    GATE_CATEGORY_SHELL,
    GATE_CATEGORY_NETWORK,
    GATE_CATEGORY_MEMORY,
    GATE_CATEGORY_SUBAGENT,
    GATE_CATEGORY_MCP,
    GATE_CATEGORY_PYTHON,
    GATE_CATEGORY_COUNT
} GateCategory;

// Shell-specific allowlist entry
typedef struct {
    char **command_prefix;  // ["git", "status"]
    int prefix_len;
} ShellAllowEntry;

// General allowlist entry
typedef struct {
    char *tool;             // Tool name (no wildcards)
    char *pattern;          // Regex for argument matching (non-shell)
    regex_t compiled;       // Pre-compiled regex
    int valid;              // Regex compilation succeeded
} AllowlistEntry;

typedef struct ApprovalChannel ApprovalChannel;  // Forward declaration

typedef struct {
    int enabled;
    GateAction categories[GATE_CATEGORY_COUNT];
    AllowlistEntry *allowlist;
    int allowlist_count;
    int allowlist_capacity;
    ShellAllowEntry *shell_allowlist;
    int shell_allowlist_count;
    int shell_allowlist_capacity;
    DenialTracker *denial_trackers;
    int denial_tracker_count;
    ApprovalChannel *approval_channel;  // NULL for root, set for subagents
} ApprovalGateConfig;

// See "Path Resolution and TOCTOU Protection" section for full definition
// This is the simplified view; the full struct includes parent_inode,
// parent_device, parent_path, is_network_fs, and Windows-specific fields
typedef struct {
    char *user_path;           // Original path from tool call
    char *resolved_path;       // Canonical path at approval time
    ino_t inode;               // Inode at approval (0 if new file)
    dev_t device;              // Device at approval
    ino_t parent_inode;        // Parent dir inode (for new files)
    dev_t parent_device;       // Parent dir device
    char *parent_path;         // Resolved parent path
    int existed;               // File existed at approval time
    int is_network_fs;         // Detected as NFS/CIFS/etc
} ApprovedPath;

typedef enum {
    APPROVAL_ALLOWED,
    APPROVAL_DENIED,
    APPROVAL_ALLOWED_ALWAYS,
    APPROVAL_ABORTED,
    APPROVAL_RATE_LIMITED
} ApprovalResult;
```

### Protected Files

Protected file detection must handle cross-platform path differences and dynamically-created files.

#### Path Normalization

```c
typedef struct {
    char *normalized;       // Normalized path (forward slashes, lowercase on Windows)
    char *basename;         // Final component
    int is_absolute;
} NormalizedPath;

NormalizedPath *normalize_path(const char *path) {
    NormalizedPath *np = calloc(1, sizeof(*np));
    char *work = strdup(path);

#ifdef _WIN32
    // Convert backslashes to forward slashes
    for (char *p = work; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    // Lowercase the entire path (Windows is case-insensitive)
    for (char *p = work; *p; p++) {
        *p = tolower((unsigned char)*p);
    }

    // Handle drive letters: C: -> /c/
    if (isalpha(work[0]) && work[1] == ':') {
        char *new_work = malloc(strlen(work) + 2);
        new_work[0] = '/';
        new_work[1] = tolower(work[0]);
        strcpy(new_work + 2, work + 2);
        free(work);
        work = new_work;
    }

    // Handle UNC paths: //server/share -> /unc/server/share
    if (work[0] == '/' && work[1] == '/') {
        char *new_work = malloc(strlen(work) + 5);
        strcpy(new_work, "/unc");
        strcat(new_work, work + 1);
        free(work);
        work = new_work;
    }

    np->is_absolute = (work[0] == '/');
#else
    // POSIX: paths are case-sensitive, already use forward slashes
    np->is_absolute = (work[0] == '/');
#endif

    // Remove trailing slashes
    size_t len = strlen(work);
    while (len > 1 && work[len - 1] == '/') {
        work[--len] = '\0';
    }

    // Remove duplicate slashes
    char *dst = work, *src = work;
    while (*src) {
        if (*src == '/' && *(src + 1) == '/') {
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    np->normalized = work;
    np->basename = strrchr(np->normalized, '/');
    np->basename = np->basename ? np->basename + 1 : np->normalized;

    return np;
}

void free_normalized_path(NormalizedPath *np) {
    if (np) {
        free(np->normalized);
        free(np);
    }
}
```

#### Protected File Patterns (Cross-Platform)

```c
// Patterns use forward slashes; matching normalizes input first
static const char *PROTECTED_BASENAME_PATTERNS[] = {
    "ralph.config.json",
    ".env",
    NULL
};

// Prefix patterns for .env.* files
static const char *PROTECTED_PREFIX_PATTERNS[] = {
    ".env.",      // .env.local, .env.production, etc.
    NULL
};

// Full path patterns (after normalization)
static const char *PROTECTED_PATH_PATTERNS[] = {
    "**/ralph.config.json",
    "**/.ralph/config.json",
    "**/.env",
    "**/.env.*",
    NULL
};
```

#### Inode Tracking with Refresh

```c
typedef struct {
    dev_t device;
    ino_t inode;
#ifdef _WIN32
    DWORD volume_serial;
    DWORD index_high;
    DWORD index_low;
#endif
    char *original_path;    // Path when first discovered
    time_t discovered_at;   // When this inode was recorded
} ProtectedInode;

typedef struct {
    ProtectedInode *inodes;
    int count;
    int capacity;
    time_t last_refresh;
} ProtectedInodeCache;

static ProtectedInodeCache inode_cache = {0};

// Refresh interval: re-scan for protected files periodically
#define INODE_REFRESH_INTERVAL 30  // seconds

void refresh_protected_inodes(void) {
    time_t now = time(NULL);
    if (now - inode_cache.last_refresh < INODE_REFRESH_INTERVAL) {
        return;  // Still fresh
    }

    // Clear existing entries
    for (int i = 0; i < inode_cache.count; i++) {
        free(inode_cache.inodes[i].original_path);
    }
    inode_cache.count = 0;

    // Scan common locations for protected files
    const char *scan_paths[] = {
        "ralph.config.json",
        ".ralph/config.json",
        ".env",
        ".env.local",
        ".env.development",
        ".env.production",
        ".env.test",
        NULL
    };

    for (int i = 0; scan_paths[i]; i++) {
        add_protected_inode_if_exists(scan_paths[i]);
    }

    // Also scan parent directories up to 3 levels
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        for (int depth = 0; depth < 3; depth++) {
            for (int i = 0; scan_paths[i]; i++) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", cwd, scan_paths[i]);
                add_protected_inode_if_exists(full_path);
            }
            // Move to parent
            char *slash = strrchr(cwd, '/');
            if (slash && slash != cwd) {
                *slash = '\0';
            } else {
                break;
            }
        }
    }

    inode_cache.last_refresh = now;
}

void add_protected_inode_if_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return;

    // Check if already tracked
    for (int i = 0; i < inode_cache.count; i++) {
        if (inode_cache.inodes[i].device == st.st_dev &&
            inode_cache.inodes[i].inode == st.st_ino) {
            return;  // Already tracked
        }
    }

    // Add to cache
    if (inode_cache.count >= inode_cache.capacity) {
        inode_cache.capacity = inode_cache.capacity ? inode_cache.capacity * 2 : 16;
        inode_cache.inodes = realloc(inode_cache.inodes,
                                      inode_cache.capacity * sizeof(ProtectedInode));
    }

    ProtectedInode *pi = &inode_cache.inodes[inode_cache.count++];
    pi->device = st.st_dev;
    pi->inode = st.st_ino;
    pi->original_path = strdup(path);
    pi->discovered_at = time(NULL);

#ifdef _WIN32
    // Get Windows file ID
    HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileInformationByHandle(h, &info)) {
            pi->volume_serial = info.dwVolumeSerialNumber;
            pi->index_high = info.nFileIndexHigh;
            pi->index_low = info.nFileIndexLow;
        }
        CloseHandle(h);
    }
#endif
}
```

#### Unified Protection Check

```c
int is_protected_file(const char *path) {
    // Refresh inode cache if stale
    refresh_protected_inodes();

    NormalizedPath *np = normalize_path(path);

    // 1. Check basename against exact patterns
    for (int i = 0; PROTECTED_BASENAME_PATTERNS[i]; i++) {
#ifdef _WIN32
        if (strcasecmp(np->basename, PROTECTED_BASENAME_PATTERNS[i]) == 0) {
#else
        if (strcmp(np->basename, PROTECTED_BASENAME_PATTERNS[i]) == 0) {
#endif
            free_normalized_path(np);
            return 1;
        }
    }

    // 2. Check basename against prefix patterns (.env.*)
    for (int i = 0; PROTECTED_PREFIX_PATTERNS[i]; i++) {
        size_t plen = strlen(PROTECTED_PREFIX_PATTERNS[i]);
#ifdef _WIN32
        if (strncasecmp(np->basename, PROTECTED_PREFIX_PATTERNS[i], plen) == 0) {
#else
        if (strncmp(np->basename, PROTECTED_PREFIX_PATTERNS[i], plen) == 0) {
#endif
            free_normalized_path(np);
            return 1;
        }
    }

    // 3. Check full path against glob patterns
    for (int i = 0; PROTECTED_PATH_PATTERNS[i]; i++) {
        int flags = FNM_PATHNAME;
#ifdef _WIN32
        flags |= FNM_CASEFOLD;  // Case-insensitive on Windows
#endif
        if (fnmatch(PROTECTED_PATH_PATTERNS[i], np->normalized, flags) == 0) {
            free_normalized_path(np);
            return 1;
        }
    }

    // 4. Check by inode (catches hardlinks and renames)
    struct stat st;
    if (stat(path, &st) == 0) {
        for (int i = 0; i < inode_cache.count; i++) {
            if (st.st_dev == inode_cache.inodes[i].device &&
                st.st_ino == inode_cache.inodes[i].inode) {
                free_normalized_path(np);
                return 1;
            }
        }
    }

#ifdef _WIN32
    // 5. Windows: also check by file index
    HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileInformationByHandle(h, &info)) {
            for (int i = 0; i < inode_cache.count; i++) {
                if (info.dwVolumeSerialNumber == inode_cache.inodes[i].volume_serial &&
                    info.nFileIndexHigh == inode_cache.inodes[i].index_high &&
                    info.nFileIndexLow == inode_cache.inodes[i].index_low) {
                    CloseHandle(h);
                    free_normalized_path(np);
                    return 1;
                }
            }
        }
        CloseHandle(h);
    }
#endif

    free_normalized_path(np);
    return 0;
}
```

#### Late-Created File Handling

Protected files created after ralph starts are caught by:

1. **Periodic inode refresh** - Every 30 seconds, re-scan known locations
2. **On-demand basename check** - Always check basename regardless of inode cache
3. **Pre-operation refresh** - Before any gated file operation, refresh the cache

```c
// Called before processing a batch of tool calls
void pre_batch_protection_refresh(void) {
    // Force immediate refresh before potentially destructive operations
    inode_cache.last_refresh = 0;
    refresh_protected_inodes();
}
```

### Tool Category Mapping

```c
// Map tool names to categories
GateCategory get_tool_category(const char *tool_name) {
    // C native tools
    if (strcmp(tool_name, "remember") == 0) return GATE_CATEGORY_MEMORY;
    if (strcmp(tool_name, "recall_memories") == 0) return GATE_CATEGORY_MEMORY;
    if (strcmp(tool_name, "forget_memory") == 0) return GATE_CATEGORY_MEMORY;
    if (strcmp(tool_name, "process_pdf_document") == 0) return GATE_CATEGORY_FILE_READ;
    if (strcmp(tool_name, "python") == 0) return GATE_CATEGORY_PYTHON;
    if (strcmp(tool_name, "subagent") == 0) return GATE_CATEGORY_SUBAGENT;
    if (strcmp(tool_name, "subagent_status") == 0) return GATE_CATEGORY_SUBAGENT;
    if (strcmp(tool_name, "todo") == 0) return GATE_CATEGORY_MEMORY;
    if (strncmp(tool_name, "vector_db_", 10) == 0) return GATE_CATEGORY_MEMORY;
    if (strncmp(tool_name, "mcp_", 4) == 0) return GATE_CATEGORY_MCP;

    // Python file tools - check for known tools, else default to python category
    return GATE_CATEGORY_PYTHON;
}
```

### Integration Point

Modify `tool_executor.c` to check gates before execution:

```c
// In tool_executor_run_workflow(), before execute_tool_call():

// Check protected files first (hard block)
if (is_file_write_tool(tool_calls[i].name)) {
    const char *path = extract_path_argument(&tool_calls[i]);
    if (path && is_protected_file(path)) {
        results[i].tool_call_id = strdup(tool_calls[i].id);
        results[i].result = strdup("{\"error\": \"protected_file\", \"message\": \"Cannot modify protected configuration file\"}");
        results[i].success = 0;
        continue;
    }
}

// Check rate limiting
if (is_rate_limited(&session->gate_config, &tool_calls[i])) {
    results[i].tool_call_id = strdup(tool_calls[i].id);
    results[i].result = format_rate_limit_error(&session->gate_config, &tool_calls[i]);
    results[i].success = 0;
    continue;
}

// Check approval gate
ApprovedPath approved_path = {0};
ApprovalResult approval = check_approval_gate(
    &session->gate_config,
    &tool_calls[i],
    &approved_path
);

switch (approval) {
    case APPROVAL_ALLOWED:
    case APPROVAL_ALLOWED_ALWAYS:
        // Verify path hasn't changed (TOCTOU protection)
        if (approved_path.resolved_path && !verify_approved_path(&approved_path)) {
            results[i].tool_call_id = strdup(tool_calls[i].id);
            results[i].result = strdup("{\"error\": \"path_changed\", \"message\": \"Path changed between approval and execution\"}");
            results[i].success = 0;
            free_approved_path(&approved_path);
            continue;
        }
        // Proceed with execution
        break;

    case APPROVAL_DENIED:
        track_denial(&session->gate_config, &tool_calls[i]);
        results[i].tool_call_id = strdup(tool_calls[i].id);
        results[i].result = strdup("{\"error\": \"operation_denied\", \"message\": \"User denied permission\"}");
        results[i].success = 0;
        free_approved_path(&approved_path);
        continue;

    case APPROVAL_RATE_LIMITED:
        results[i].tool_call_id = strdup(tool_calls[i].id);
        results[i].result = format_rate_limit_error(&session->gate_config, &tool_calls[i]);
        results[i].success = 0;
        continue;

    case APPROVAL_ABORTED:
        free_approved_path(&approved_path);
        return TOOL_EXECUTOR_ABORTED;
}

free_approved_path(&approved_path);
```

### Core Functions

```c
// Initialize gate configuration from config file
int approval_gate_init(ApprovalGateConfig *config);

// Initialize child config from parent (for subagents)
int approval_gate_init_from_parent(
    ApprovalGateConfig *child,
    const ApprovalGateConfig *parent
);

// Free gate configuration resources
void approval_gate_cleanup(ApprovalGateConfig *config);

// Check if tool call requires approval
int approval_gate_requires_check(
    const ApprovalGateConfig *config,
    const ToolCall *tool_call
);

// Prompt user and get decision
ApprovalResult approval_gate_prompt(
    ApprovalGateConfig *config,
    const ToolCall *tool_call,
    ApprovedPath *out_path
);

// Combined check and prompt
ApprovalResult check_approval_gate(
    ApprovalGateConfig *config,
    const ToolCall *tool_call,
    ApprovedPath *out_path
);

// Add pattern to session allowlist
int approval_gate_add_allowlist(
    ApprovalGateConfig *config,
    const char *tool,
    const char *pattern
);

// Add shell command to session allowlist
int approval_gate_add_shell_allowlist(
    ApprovalGateConfig *config,
    const char **command_prefix,
    int prefix_len
);

// Check if tool call matches allowlist
int approval_gate_matches_allowlist(
    const ApprovalGateConfig *config,
    const ToolCall *tool_call
);

// Parse shell command for matching
ParsedShellCommand *parse_shell_command(const char *command);
void free_parsed_shell_command(ParsedShellCommand *cmd);

// Check for dangerous shell patterns
int shell_command_is_dangerous(const char *command);

// Rate limiting
int is_rate_limited(const ApprovalGateConfig *config, const ToolCall *tool_call);
void track_denial(ApprovalGateConfig *config, const ToolCall *tool_call);
void reset_denial_tracker(ApprovalGateConfig *config, const char *tool);

// TOCTOU protection
int verify_approved_path(const ApprovedPath *approved);
void free_approved_path(ApprovedPath *path);
```

## Testing

### Unit Tests

- Shell command parsing and chain detection
- Shell allowlist matching (prefix matching, rejection of chained commands)
- Dangerous pattern detection
- Category classification for all built-in tools
- Config parsing from JSON
- Pattern generation from tool calls
- Protected file detection
- Path resolution and TOCTOU verification
- Rate limiting backoff calculations

#### Cross-Platform Shell Parsing Tests

- cmd.exe metacharacter detection (`&`, `|`, `%VAR%`, `^`)
- PowerShell dangerous cmdlet detection (`Invoke-Expression`, `-EncodedCommand`)
- Shell type detection from environment
- Command equivalence matching (`ls` ↔ `dir` ↔ `Get-ChildItem`)
- Allowlist entry shell-type filtering

#### Path Normalization Tests

- Windows backslash to forward slash conversion
- Windows case-insensitive comparison
- Drive letter normalization (`C:` → `/c/`)
- UNC path handling (`//server/share` → `/unc/server/share`)
- Duplicate slash removal
- Trailing slash handling

#### Protected File Tests

- Basename pattern matching (case-sensitive on POSIX, insensitive on Windows)
- Prefix pattern matching (`.env.*` files)
- Glob pattern matching with `fnmatch`
- Inode-based detection of hardlinks
- Windows file index-based detection
- Late-created file detection via refresh
- Cross-platform `.env` variant detection

#### Atomic File Operations Tests

- `O_NOFOLLOW` rejects symlinks at final component
- `O_EXCL` fails on existing files
- `openat()` uses verified parent fd
- `fstat()` inode verification after open
- Symlink swap between approval and execution (TOCTOU)
- Parent directory inode verification for new files
- Network filesystem detection and warning

### Integration Tests

- Interactive approval flow (requires TTY mocking)
- Batch approval with multiple tools
- Session allowlist persistence
- Non-interactive mode behavior (deny by default)
- Subagent gate inheritance
- Subagent TTY prompt forwarding
- Protected file rejection

#### Subagent Deadlock Prevention Tests

- Parent receives and displays subagent approval requests
- Subagent blocks until parent responds
- Timeout causes denial (5 minute default)
- Nested subagent approval forwarding (3 levels deep)
- Parent crash causes subagent timeout and denial
- Multiple concurrent subagents with interleaved approvals
- "Allow always" propagates pattern to parent session allowlist

#### Cross-Platform Integration Tests

- Run shell commands on Windows cmd.exe (if available)
- Run shell commands on PowerShell (if available)
- Protected file detection on case-insensitive filesystem
- Path with spaces and special characters
- Long path handling (> 260 chars on Windows)

## Future Extensions

1. **Persistent allowlists**: Save approved patterns across sessions
2. **Project-specific rules**: `.ralph/gates.json` in project root
3. **Risk scoring**: Show risk level based on operation type
4. **Undo support**: Track reversible operations for rollback
5. **Audit log**: Record all approved/denied operations

## File Structure

```
src/
  core/
    approval_gate.h         # Public interface
    approval_gate.c         # Implementation
    shell_parser.h          # Shell command parsing (unified interface)
    shell_parser.c          # POSIX shell parsing implementation
    shell_parser_cmd.c      # Windows cmd.exe parsing
    shell_parser_ps.c       # PowerShell parsing
    protected_files.h       # Protected file interface
    protected_files.c       # Protected file checking
    path_normalize.h        # Cross-platform path normalization
    path_normalize.c        # Path normalization implementation
    atomic_file.h           # Atomic file operations
    atomic_file.c           # TOCTOU-safe file operations
    subagent_approval.h     # Subagent approval proxy
    subagent_approval.c     # IPC for subagent approvals
    tool_executor.c         # Modified to call gates
test/
  test_approval_gate.c      # Unit tests
  test_shell_parser.c       # POSIX shell parsing tests
  test_shell_parser_cmd.c   # cmd.exe parsing tests
  test_shell_parser_ps.c    # PowerShell parsing tests
  test_path_normalize.c     # Path normalization tests
  test_protected_files.c    # Protected file tests
  test_atomic_file.c        # TOCTOU protection tests
  test_subagent_approval.c  # Subagent deadlock prevention tests
```
