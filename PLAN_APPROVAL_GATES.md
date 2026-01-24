# Approval Gates Implementation Plan

This document provides a flat list of implementation tasks for the Approval Gates feature. Each task includes enough context for an agent to break it down into executable steps. Cross-references to `SPEC_APPROVAL_GATES.md` are provided in parentheses.

Reference: `./SPEC_APPROVAL_GATES.md`

---

## Core Data Structures

- [x] **Create `src/core/approval_gate.h`** - Define all public data structures and function prototypes. Include `GateAction` enum (`ALLOW`, `GATE`, `DENY`), `GateCategory` enum (8 categories), `ApprovalResult` enum, `ApprovalGateConfig` struct, `AllowlistEntry` struct, `ShellAllowEntry` struct, `DenialTracker` struct. See spec section "Implementation > Data Structures" for complete definitions.

- [ ] **Create `src/core/approval_gate.c`** - Implement core approval gate logic including initialization, cleanup, category lookup, and the main `check_approval_gate()` function that orchestrates the approval flow. See spec section "Implementation > Core Functions" for function signatures.

---

## Configuration System

- [ ] **Extend `src/utils/config.c`** - Add parsing for the `approval_gates` section of `ralph.config.json`. Parse `enabled` boolean, `categories` object (map category names to actions), and `allowlist` array. Store in a new config structure accessible to approval gate module. See spec section "Configuration > Config File" for JSON schema.

- [ ] **Add CLI flag parsing in `src/core/main.c`** - Implement `--yolo` flag to disable all gates for session. Implement `--allow "tool:arg1,arg2"` to add entries to session allowlist. Implement `--allow-category=<category>` to override category defaults. CLI flags must override config file settings. See spec section "Configuration > Command-Line Flags".

- [ ] **Implement category default initialization** - Set up default actions for each category: `file_write=gate`, `file_read=allow`, `shell=gate`, `network=gate`, `memory=allow`, `subagent=gate`, `mcp=gate`, `python=allow`. See spec section "Tool Categories" table.

---

## Tool Category Mapping

- [ ] **Implement `get_tool_category()` function** - Map tool names to `GateCategory` enum. Handle C native tools by exact name match (`remember`, `recall_memories`, `forget_memory`, `process_pdf_document`, `python`, `subagent`, `subagent_status`, `todo`, `vector_db_*` prefix, `mcp_*` prefix). Default unknown tools to `GATE_CATEGORY_PYTHON`. See spec section "Implementation > Tool Category Mapping".

- [ ] **Handle Python file tool categorization** - Parse `Gate:` directive from Python tool docstrings to override default category. Parse `Match:` directive to identify which argument to use for pattern matching. See spec section "Dynamic Tools > Gate Metadata" for directive format.

---

## Path Normalization (Cross-Platform)

- [ ] **Create `src/core/path_normalize.h`** - Define `NormalizedPath` struct with `normalized`, `basename`, and `is_absolute` fields. Declare `normalize_path()` and `free_normalized_path()` functions.

- [ ] **Create `src/core/path_normalize.c`** - Implement cross-platform path normalization. On Windows: convert backslashes to forward slashes, lowercase entire path, convert drive letters (`C:` → `/c/`), handle UNC paths (`//server/share` → `/unc/server/share`). On POSIX: minimal normalization. Both: remove trailing slashes, collapse duplicate slashes, extract basename. See spec section "Implementation > Protected Files > Path Normalization".

---

## Protected File Detection

- [ ] **Create `src/core/protected_files.h`** - Define `ProtectedInode` struct and `ProtectedInodeCache` struct. Declare `is_protected_file()`, `refresh_protected_inodes()`, and related functions.

- [ ] **Create `src/core/protected_files.c`** - Implement protected file detection with multiple strategies: (1) basename exact match (`ralph.config.json`, `.env`), (2) basename prefix match (`.env.*`), (3) glob pattern match (`**/ralph.config.json`, `**/.ralph/config.json`, `**/.env`, `**/.env.*`), (4) inode-based detection for hardlinks/renames. See spec section "Protected Files" and "Implementation > Protected Files".

- [ ] **Implement inode cache with periodic refresh** - Scan common locations for protected files every 30 seconds. Track device+inode on POSIX, volume serial + file index on Windows. Implement `add_protected_inode_if_exists()` to populate cache. See spec section "Implementation > Protected Files > Inode Tracking with Refresh".

- [ ] **Add pre-batch cache refresh** - Implement `pre_batch_protection_refresh()` to force cache refresh before processing tool call batches. Ensures late-created protected files are detected. See spec section "Implementation > Protected Files > Late-Created File Handling".

- [ ] **Windows file identity support** - Use `GetFileInformationByHandle()` to get `nFileIndexHigh`, `nFileIndexLow`, `dwVolumeSerialNumber` for Windows file identity tracking. Conditional compilation with `#ifdef _WIN32`. See spec section "Implementation > Protected Files > Unified Protection Check".

---

## Shell Command Parsing

- [ ] **Create `src/core/shell_parser.h`** - Define `ShellType` enum (`POSIX`, `CMD`, `POWERSHELL`, `UNKNOWN`), `ParsedShellCommand` struct with `tokens`, `token_count`, `has_chain`, `has_pipe`, `has_subshell`, `has_redirect`, `is_dangerous`, `shell_type` fields. Declare unified `parse_shell_command()` and `detect_shell_type()` functions.

- [ ] **Implement `detect_shell_type()`** - On Windows: check `COMSPEC` and `PSModulePath` environment variables to distinguish cmd.exe vs PowerShell. On POSIX: check `SHELL` for pwsh/powershell, default to POSIX. See spec section "Cross-Platform Shell Parsing > Shell Detection".

- [ ] **Create `src/core/shell_parser.c`** - Implement POSIX shell parsing. Tokenize on unquoted whitespace, respect single and double quotes, detect metacharacters (`;`, `|`, `&`, `(`, `)`, `$`, backtick, `>`, `<`). Mark command as having chains/pipes/subshells if any metacharacter appears unquoted. See spec section "Shell Command Matching > Parser Security Requirements".

- [ ] **Create `src/core/shell_parser_cmd.c`** - Implement cmd.exe parsing. Detect metacharacters (`&`, `|`, `<`, `>`, `^`, `%`). Only double quotes are string delimiters. Implement `in_double_quotes()` helper. See spec section "Cross-Platform Shell Parsing > cmd.exe Parsing Rules".

- [ ] **Create `src/core/shell_parser_ps.c`** - Implement PowerShell parsing. Detect all POSIX-like operators plus script blocks (`{}`), variables (`$var`), invoke operators (`&`, `.`). Implement `powershell_command_is_dangerous()` to check for dangerous cmdlets (`Invoke-Expression`, `Invoke-Command`, `Start-Process`, `-EncodedCommand`, `DownloadString`, etc.). Case-insensitive matching. See spec section "Cross-Platform Shell Parsing > PowerShell Parsing Rules".

- [ ] **Implement dangerous pattern detection** - Check raw command string against known dangerous patterns before parsing: `rm -rf`, `rm -fr`, `> /dev/sd*`, `dd if=* of=/dev/*`, `chmod 777`, `chmod -R`, `curl * | *sh`, `wget * | *sh`, fork bomb pattern. These always require approval regardless of allowlist. See spec section "Shell Command Matching > Dangerous Pattern Detection".

---

## Allowlist Matching

- [ ] **Implement shell command allowlist matching** - Shell allowlist entries use parsed command prefix matching, not regex. Entry `["git", "status"]` matches `git status`, `git status -s`, but NOT `git status; rm -rf /`. Commands with chain operators never match allowlist. See spec section "Shell Command Matching > Allowlist Entry Format".

- [ ] **Implement regex allowlist matching for non-shell tools** - Compile regex patterns at config load time. Match against tool's match target (path argument for file tools, query for memory tools, etc.). See spec section "Allowlist Pattern Syntax".

- [ ] **Implement shell-type-specific allowlist entries** - Allowlist entries can optionally specify `"shell": "posix"`, `"shell": "cmd"`, or `"shell": "powershell"`. When specified, entry only matches that shell type. When omitted, matches any shell. See spec section "Cross-Platform Shell Parsing > Allowlist Matching Per Shell".

- [ ] **Implement command equivalence matching** - Recognize cross-platform equivalents: `ls`↔`dir`↔`Get-ChildItem`, `cat`↔`type`↔`Get-Content`, etc. When allowlist contains one, match the equivalent on other platforms. See spec section "Cross-Platform Shell Parsing > Safe Command Equivalents".

---

## TOCTOU Protection

- [ ] **Create `src/core/atomic_file.h`** - Define `ApprovedPath` struct with fields for user path, resolved path, inode, device, parent inode/device (for new files), `existed` flag, `is_network_fs` flag, and Windows-specific fields. Define `VERIFY_OK`, `VERIFY_ERR_*` error codes.

- [ ] **Create `src/core/atomic_file.c`** - Implement atomic file operations using `O_NOFOLLOW`, `O_EXCL`, `openat()`, and `fstat()` verification. For existing files: open with `O_NOFOLLOW`, verify inode/device match approval. For new files: verify parent directory inode, create with `O_EXCL`. See spec section "Path Resolution and TOCTOU Protection > Atomic File Operations".

- [ ] **Implement `verify_and_open_approved_path()`** - Unified function that handles both existing and new file verification, returns file descriptor on success. See spec section "Path Resolution and TOCTOU Protection > Verification Flow".

- [ ] **Windows atomic file operations** - Use `CreateFileW` with `FILE_FLAG_OPEN_REPARSE_POINT` to detect symlinks/junctions. Use `CREATE_NEW` for `O_EXCL` equivalent. Check `FILE_ATTRIBUTE_REPARSE_POINT` in file attributes. See spec section "Path Resolution and TOCTOU Protection > Platform-Specific Considerations > Windows".

- [ ] **Network filesystem detection** - Detect NFS/CIFS/SMB mounts by checking `/proc/mounts` on Linux, `GetVolumeInformation()` on Windows. Set `is_network_fs` flag in `ApprovedPath`. Optionally warn user about unreliable verification. See spec section "Path Resolution and TOCTOU Protection > Platform-Specific Considerations > Network Filesystems".

---

## User Prompt Interface

- [ ] **Implement TTY approval prompt** - Display formatted prompt box showing tool name, arguments/command, and options: `[y] Allow`, `[n] Deny`, `[a] Allow always`, `[?] Details`. Read single keypress response. See spec section "User Prompt Interface".

- [ ] **Implement details view** - When user presses `?`, display full arguments JSON and resolved paths. Return to prompt after display.

- [ ] **Implement batch approval** - When multiple tool calls are pending, show numbered list of operations. Support `[y] Allow all`, `[n] Deny all`, `[1-N] Review individual`. See spec section "User Prompt Interface > Batch Approval".

- [ ] **Handle Ctrl+C in prompt** - Return `APPROVAL_ABORTED` to signal workflow should stop without executing. See spec section "User Prompt Interface > Options" table.

---

## Allow Always Pattern Generation

- [ ] **Implement file path pattern generation** - Extract directory component, generate pattern matching that directory and similar extensions. Root files get exact match. `/tmp` paths get exact match (security). See spec section "Allow Always Pattern Generation > Strategy" table for examples.

- [ ] **Implement shell command pattern generation** - Add base command and first argument to allowlist. Never auto-allow commands with pipes, chains, or redirects. Generate `ShellAllowEntry` with command prefix. See spec section "Allow Always Pattern Generation > Strategy" for shell examples.

- [ ] **Implement network URL pattern generation** - Parse URL, extract scheme + hostname, generate pattern that matches scheme, exact hostname, and requires path separator: `^https://api\.example\.com(/|$)`. Prevents subdomain spoofing. See spec section "Allow Always Pattern Generation > Strategy".

- [ ] **Implement pattern confirmation dialog** - When generated pattern would match more than current operation, show confirmation with example matches. Offer `[y] Confirm`, `[e] Edit pattern`, `[x] Exact match only`. See spec section "Allow Always Pattern Generation > User Confirmation for Broad Patterns".

---

## Denial Rate Limiting

- [ ] **Implement `DenialTracker` management** - Track per-tool denial count, last denial time, backoff expiry. Store in `ApprovalGateConfig.denial_trackers` array.

- [ ] **Implement exponential backoff** - 1-2 denials: no backoff. 3: 5s. 4: 15s. 5: 60s. 6+: 5 minutes. See spec section "Denial Rate Limiting > Backoff Schedule".

- [ ] **Implement `is_rate_limited()` check** - Return true if tool is in backoff period. Called before prompting user.

- [ ] **Implement rate limit error response** - Return structured JSON error with `retry_after` field. See spec section "Denial Rate Limiting > Behavior During Backoff".

- [ ] **Implement backoff reset** - Reset denial counter on: user approval, session restart, or backoff expiry without new denial. See spec section "Denial Rate Limiting".

---

## Subagent Approval Proxy

- [ ] **Create `src/core/subagent_approval.h`** - Define `ApprovalChannel` struct with request/response file descriptors and subagent PID. Define `ApprovalRequest` and `ApprovalResponse` structs for IPC messages.

- [ ] **Create `src/core/subagent_approval.c`** - Implement IPC-based approval proxying. Parent maintains exclusive TTY ownership, subagents send approval requests via pipe, parent prompts user and sends response. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Architecture: Approval Proxy".

- [ ] **Implement `subagent_request_approval()`** - Subagent-side function that serializes request, writes to pipe, blocks waiting for response with timeout (5 minutes). Timeout results in denial. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Subagent Side".

- [ ] **Implement `parent_approval_loop()`** - Parent-side multiplexing using `select()` to monitor stdin and all subagent request pipes. Handle interleaved approvals from multiple concurrent subagents. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Parent Side".

- [ ] **Implement `handle_subagent_approval_request()`** - Parent receives request, displays prompt (noting it's from subagent with PID), gets user response, sends back to subagent. If "Allow always", add pattern to parent's session allowlist. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Parent Side".

- [ ] **Modify `spawn_subagent_with_approval()` in subagent_tool.c** - Create request/response pipes before fork. Child closes parent ends, sets up `ApprovalChannel`. Parent closes child ends, adds channel to monitoring set. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Spawn with Channels".

- [ ] **Implement nested subagent approval forwarding** - When a subagent spawns its own subagent, requests are forwarded up the chain to root parent. Check `config->approval_channel` to determine if we're a subagent. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Nested Subagents".

- [ ] **Implement gate inheritance for subagents** - Subagents inherit parent's category config and static allowlist, but NOT session allowlist. Implement `approval_gate_init_from_parent()`. See spec section "Subagent Behavior > Gate Inheritance".

---

## Non-Interactive Mode

- [ ] **Detect non-TTY stdin** - Check `isatty(STDIN_FILENO)` at startup. If false, set non-interactive mode.

- [ ] **Implement non-interactive gate behavior** - Gated categories default to `deny` when no TTY available. Allowed categories proceed normally. Return structured error to LLM. See spec section "Non-Interactive Mode > Non-TTY Behavior".

- [ ] **Support partial trust flags** - `--allow-category=<category>` allows specific categories without prompting in non-interactive mode. `--allow="tool:args"` allows specific patterns. See spec section "Non-Interactive Mode > Partial Trust".

---

## Error Messages

- [ ] **Implement structured denial error** - Return JSON with `error: "operation_denied"`, `message`, `tool`, `suggestion` field advising LLM to ask user or explain need. See spec section "Error Messages".

- [ ] **Implement protected file error** - Return JSON with `error: "protected_file"`, `message: "Cannot modify protected configuration file"`, `path`. See spec section "Protected Files".

- [ ] **Implement rate limit error** - Return JSON with `error: "rate_limited"`, `message`, `retry_after` seconds, `tool`. See spec section "Denial Rate Limiting > Behavior During Backoff".

- [ ] **Implement path changed error** - Return JSON with `error: "path_changed"`, `message: "Path changed between approval and execution"` for TOCTOU violations.

---

## Integration with Tool Executor

- [ ] **Modify `tool_executor.c` to check gates** - Before `execute_tool_call()`, check protected files first (hard block), then rate limiting, then approval gate. Handle all `ApprovalResult` cases. See spec section "Implementation > Integration Point" for code structure.

- [ ] **Pass `ApprovedPath` to file tools** - When approval includes path verification, pass the `ApprovedPath` struct to file tools so they use `verify_and_open_approved_path()` instead of direct `open()`.

- [ ] **Add gate config to `RalphSession`** - Store `ApprovalGateConfig` in session structure. Initialize from config + CLI flags at session start.

- [ ] **Handle `APPROVAL_ABORTED`** - When user presses Ctrl+C during prompt, return from tool executor with abort status, don't process remaining tool calls.

---

## Unit Tests

- [ ] **Create `test/test_approval_gate.c`** - Test gate config initialization, category lookup, allowlist matching (regex), rate limiting calculations, denial tracking.

- [ ] **Create `test/test_shell_parser.c`** - Test POSIX shell tokenization, quote handling, metacharacter detection, chain/pipe detection, dangerous pattern matching. Test edge cases: empty commands, only whitespace, unbalanced quotes.

- [ ] **Create `test/test_shell_parser_cmd.c`** - Test cmd.exe parsing: `&` as separator, `%VAR%` detection, `^` escape, double-quote handling. Test dangerous patterns.

- [ ] **Create `test/test_shell_parser_ps.c`** - Test PowerShell parsing: cmdlet detection (case-insensitive), `-EncodedCommand`, `$()` subexpressions, script blocks. Test all dangerous cmdlets from spec.

- [ ] **Create `test/test_path_normalize.c`** - Test Windows backslash conversion, case handling, drive letter normalization (`C:` → `/c/`), UNC paths (`//server/share` → `/unc/server/share`), duplicate slash removal, trailing slash handling.

- [ ] **Create `test/test_protected_files.c`** - Test basename matching, prefix matching (`.env.*`), glob patterns, inode-based detection. Test case sensitivity differences between platforms.

- [ ] **Create `test/test_atomic_file.c`** - Test `O_NOFOLLOW` symlink rejection, `O_EXCL` existing file failure, `openat()` parent verification, inode mismatch detection. Create actual temp files/symlinks for realistic tests.

---

## Integration Tests

- [ ] **Create `test/test_approval_gate_integration.c`** - Test end-to-end approval flow with mock TTY input. Test batch approval, allow always with pattern generation, denial rate limiting across multiple calls.

- [ ] **Create `test/test_subagent_approval.c`** - Test parent-child approval proxy. Test timeout handling (mock slow response). Test nested subagent forwarding. Test concurrent subagents with interleaved approvals. Note: these tests may need special handling per CLAUDE.md subagent test guidelines.

- [ ] **Add approval gate tests to existing tool tests** - Extend `test_tools_system.c` to verify gates are checked. Test that protected files are rejected. Test that denied operations return proper error JSON.

---

## Makefile Updates

- [ ] **Add new source files to Makefile** - Add all new `.c` files to appropriate build targets. Maintain Makefile organization per CLAUDE.md guidelines.

- [ ] **Add new test targets** - Add test executables for all new test files. Add to `make test` target. Exclude subagent approval tests from valgrind per CLAUDE.md guidelines.

---

## Documentation

- [ ] **Update ARCHITECTURE.md** - Add approval gate layer to system architecture diagram. Show integration with tool executor and subagent system.

- [ ] **Update CODE_OVERVIEW.md** - Add entries for all new files in `src/core/`. Update tool system description to mention gate checking.

- [ ] **Update CLAUDE.md** - Document `--yolo` flag usage for development. Note that approval gate tests may require TTY mocking.

---

## Memory Safety

- [ ] **Valgrind all new code** - Run valgrind on all new test files (except subagent tests per CLAUDE.md). Fix any leaks in approval gate, shell parser, path normalization, atomic file operations.

- [ ] **Ensure proper cleanup** - Implement `approval_gate_cleanup()`, `free_parsed_shell_command()`, `free_normalized_path()`, `free_approved_path()`. Call from session cleanup.

- [ ] **Handle allocation failures** - Check all `malloc`/`calloc`/`strdup` return values. Return graceful errors, don't crash.
