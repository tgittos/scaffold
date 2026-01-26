# Approval Gates Implementation Plan

This document provides a flat list of implementation tasks for the Approval Gates feature. Each task includes enough context for an agent to break it down into executable steps. Cross-references to `SPEC_APPROVAL_GATES.md` are provided in parentheses.

Reference: `./SPEC_APPROVAL_GATES.md`

**Architecture Note**: The approval gate system lives in the `src/policy/` subsystem, which uses opaque types (`RateLimiter`, `Allowlist`, `GatePrompter`) for better encapsulation and testability. Helper modules (`tool_args`, `pattern_generator`) extract shared functionality.

---

## Core Data Structures

- [x] **Create `src/policy/approval_gate.h`** - Define all public data structures and function prototypes. Include `GateAction` enum (`ALLOW`, `GATE`, `DENY`), `GateCategory` enum (8 categories), `ApprovalResult` enum (including `APPROVAL_NON_INTERACTIVE_DENIED`), `ApprovalGateConfig` struct with opaque type pointers. See spec section "Implementation > Data Structures" for complete definitions.

- [x] **Create `src/policy/approval_gate.c`** - Implement core approval gate orchestration logic including initialization, cleanup, category lookup, and the main `check_approval_gate()` function. Delegates to opaque types for rate limiting, allowlist matching, and prompting. See spec section "Implementation > Core Functions" for function signatures.

---

## Opaque Types (Policy Module)

- [x] **Create `src/policy/rate_limiter.h` and `rate_limiter.c`** - Implement opaque `RateLimiter` type that owns denial tracking data internally. Provides `rate_limiter_create()`, `rate_limiter_destroy()`, `rate_limiter_is_limited()`, `rate_limiter_track_denial()`, `rate_limiter_reset()`, `rate_limiter_get_retry_after()`. See spec section "Implementation > Opaque Types > RateLimiter".

- [x] **Create `src/policy/allowlist.h` and `allowlist.c`** - Implement opaque `Allowlist` type that owns regex and shell allowlist entries. Provides `allowlist_create()`, `allowlist_destroy()`, `allowlist_add_regex()`, `allowlist_add_shell()`, `allowlist_matches_regex()`, `allowlist_matches_shell()`, `allowlist_load_from_json()`. See spec section "Implementation > Opaque Types > Allowlist".

- [x] **Create `src/policy/gate_prompter.h` and `gate_prompter.c`** - Implement opaque `GatePrompter` type that encapsulates terminal UI handling. Provides `gate_prompter_create()`, `gate_prompter_destroy()`, `gate_prompter_single()`, `gate_prompter_batch()`, `gate_prompter_generate_pattern()`. See spec section "Implementation > Opaque Types > GatePrompter".

- [x] **Create `src/policy/tool_args.h` and `tool_args.c`** - Centralized cJSON argument extraction from ToolCall structs. Provides `tool_args_get_string()`, `tool_args_get_int()`, `tool_args_parse()`. Eliminates duplicate JSON parsing across modules.

- [x] **Create `src/policy/pattern_generator.h` and `pattern_generator.c`** - Pure functions for generating allowlist patterns without terminal I/O. Provides `pattern_generate_for_path()`, `pattern_generate_for_url()`, `pattern_generate_for_shell()`. See spec section "Allow Always Pattern Generation".

---

## Configuration System

- [x] **Extend `src/utils/config.c`** - Add parsing for the `approval_gates` section of `ralph.config.json`. Parse `enabled` boolean, `categories` object (map category names to actions), and `allowlist` array. Store in a new config structure accessible to approval gate module. See spec section "Configuration > Config File" for JSON schema.

- [x] **Add CLI flag parsing in `src/core/main.c`** - Implement `--yolo` flag to disable all gates for session. Implement `--allow "tool:arg1,arg2"` to add entries to session allowlist. Implement `--allow-category=<category>` to override category defaults. CLI flags must override config file settings. See spec section "Configuration > Command-Line Flags".

- [x] **Implement category default initialization** - Set up default actions for each category: `file_write=gate`, `file_read=allow`, `shell=gate`, `network=gate`, `memory=allow`, `subagent=gate`, `mcp=gate`, `python=allow`. See spec section "Tool Categories" table.

---

## Tool Category Mapping

- [x] **Implement `get_tool_category()` function** - Map tool names to `GateCategory` enum. Handle C native tools by exact name match (`remember`, `recall_memories`, `forget_memory`, `process_pdf_document`, `python`, `subagent`, `subagent_status`, `todo`, `vector_db_*` prefix, `mcp_*` prefix). Default unknown tools to `GATE_CATEGORY_PYTHON`. See spec section "Implementation > Tool Category Mapping".

- [x] **Handle Python file tool categorization** - Parse `Gate:` directive from Python tool docstrings to override default category. Parse `Match:` directive to identify which argument to use for pattern matching. See spec section "Dynamic Tools > Gate Metadata" for directive format.

---

## Path Normalization (Cross-Platform)

- [x] **Create `src/policy/path_normalize.h`** - Define `NormalizedPath` struct with `normalized`, `basename`, and `is_absolute` fields. Declare `normalize_path()` and `free_normalized_path()` functions.

- [x] **Create `src/policy/path_normalize.c`** - Implement cross-platform path normalization. On Windows: convert backslashes to forward slashes, lowercase entire path, convert drive letters (`C:` → `/c/`), handle UNC paths (`//server/share` → `/unc/server/share`). On POSIX: minimal normalization. Both: remove trailing slashes, collapse duplicate slashes, extract basename. See spec section "Implementation > Protected Files > Path Normalization".

---

## Protected File Detection

- [x] **Create `src/policy/protected_files.h`** - Define `ProtectedInode` struct and `ProtectedInodeCache` struct. Declare `is_protected_file()`, `refresh_protected_inodes()`, and related functions.

- [x] **Create `src/policy/protected_files.c`** - Implement protected file detection with multiple strategies: (1) basename exact match (`ralph.config.json`, `.env`), (2) basename prefix match (`.env.*`), (3) glob pattern match (recursive patterns like `**/ralph.config.json`), (4) inode-based detection for hardlinks/renames. See spec section "Protected Files" and "Implementation > Protected Files".

- [x] **Implement inode cache with periodic refresh** - Scan common locations for protected files every 30 seconds. Track device+inode on POSIX, volume serial + file index on Windows. Implement `add_protected_inode_if_exists()` to populate cache. See spec section "Implementation > Protected Files > Inode Tracking with Refresh".

- [x] **Add pre-batch cache refresh** - Implement `force_protected_inode_refresh()` to force cache refresh before processing tool call batches. Ensures late-created protected files are detected. See spec section "Implementation > Protected Files > Late-Created File Handling".

- [x] **Windows file identity support** - Use `GetFileInformationByHandle()` to get `nFileIndexHigh`, `nFileIndexLow`, `dwVolumeSerialNumber` for Windows file identity tracking. Conditional compilation with `#ifdef _WIN32`. See spec section "Implementation > Protected Files > Unified Protection Check".

---

## Shell Command Parsing

- [x] **Create `src/policy/shell_parser.h`** - Define `ShellType` enum (`POSIX`, `CMD`, `POWERSHELL`, `UNKNOWN`), `ParsedShellCommand` struct with `tokens`, `token_count`, `has_chain`, `has_pipe`, `has_subshell`, `has_redirect`, `is_dangerous`, `shell_type` fields. Declare unified `parse_shell_command()` and `detect_shell_type()` functions.

- [x] **Implement `detect_shell_type()`** - On Windows: check `COMSPEC` and `PSModulePath` environment variables to distinguish cmd.exe vs PowerShell. On POSIX: check `SHELL` for pwsh/powershell, default to POSIX. See spec section "Cross-Platform Shell Parsing > Shell Detection".

- [x] **Create `src/policy/shell_parser.c`** - Implement POSIX shell parsing. Tokenize on unquoted whitespace, respect single and double quotes, detect metacharacters (`;`, `|`, `&`, `(`, `)`, `$`, backtick, `>`, `<`). Mark command as having chains/pipes/subshells if any metacharacter appears unquoted. See spec section "Shell Command Matching > Parser Security Requirements".

- [x] **Create `src/policy/shell_parser_cmd.c`** - Implement cmd.exe parsing. Detect metacharacters (`&`, `|`, `<`, `>`, `^`, `%`). Only double quotes are string delimiters. Implement `in_double_quotes()` helper. See spec section "Cross-Platform Shell Parsing > cmd.exe Parsing Rules".

- [x] **Create `src/policy/shell_parser_ps.c`** - Implement PowerShell parsing. Detect all POSIX-like operators plus script blocks (`{}`), variables (`$var`), invoke operators (`&`, `.`). Implement `powershell_command_is_dangerous()` to check for dangerous cmdlets (`Invoke-Expression`, `Invoke-Command`, `Start-Process`, `-EncodedCommand`, `DownloadString`, etc.). Case-insensitive matching. See spec section "Cross-Platform Shell Parsing > PowerShell Parsing Rules".

- [x] **Implement dangerous pattern detection** - Check raw command string against known dangerous patterns before parsing: `rm -rf`, `rm -fr`, `> /dev/sd*`, `dd if=* of=/dev/*`, `chmod 777`, `chmod -R`, `curl * | *sh`, `wget * | *sh`, fork bomb pattern. These always require approval regardless of allowlist. See spec section "Shell Command Matching > Dangerous Pattern Detection".

---

## Allowlist Matching

- [x] **Implement shell command allowlist matching** - Shell allowlist entries use parsed command prefix matching, not regex. Entry `["git", "status"]` matches `git status`, `git status -s`, but NOT `git status; rm -rf /`. Commands with chain operators never match allowlist. See spec section "Shell Command Matching > Allowlist Entry Format".

- [x] **Implement regex allowlist matching for non-shell tools** - Compile regex patterns at config load time. Match against tool's match target (path argument for file tools, query for memory tools, etc.). See spec section "Allowlist Pattern Syntax".

- [x] **Implement shell-type-specific allowlist entries** - Allowlist entries can optionally specify `"shell": "posix"`, `"shell": "cmd"`, or `"shell": "powershell"`. When specified, entry only matches that shell type. When omitted, matches any shell. See spec section "Cross-Platform Shell Parsing > Allowlist Matching Per Shell".

- [x] **Implement command equivalence matching** - Recognize cross-platform equivalents: `ls`↔`dir`↔`Get-ChildItem`, `cat`↔`type`↔`Get-Content`, etc. When allowlist contains one, match the equivalent on other platforms. See spec section "Cross-Platform Shell Parsing > Safe Command Equivalents".

---

## TOCTOU Protection

- [x] **Create `src/policy/atomic_file.h`** - Define `ApprovedPath` struct with fields for user path, resolved path, inode, device, parent inode/device (for new files), `existed` flag, `is_network_fs` flag, and Windows-specific fields. Define `VERIFY_OK`, `VERIFY_ERR_*` error codes.

- [x] **Create `src/policy/atomic_file.c`** - Implement atomic file operations using `O_NOFOLLOW`, `O_EXCL`, `openat()`, and `fstat()` verification. For existing files: open with `O_NOFOLLOW`, verify inode/device match approval. For new files: verify parent directory inode, create with `O_EXCL`. See spec section "Path Resolution and TOCTOU Protection > Atomic File Operations".

- [x] **Implement `verify_and_open_approved_path()`** - Unified function that handles both existing and new file verification, returns file descriptor on success. See spec section "Path Resolution and TOCTOU Protection > Verification Flow".

- [x] **Windows atomic file operations** - Use `CreateFileW` with `FILE_FLAG_OPEN_REPARSE_POINT` to detect symlinks/junctions. Use `CREATE_NEW` for `O_EXCL` equivalent. Check `FILE_ATTRIBUTE_REPARSE_POINT` in file attributes. See spec section "Path Resolution and TOCTOU Protection > Platform-Specific Considerations > Windows".

- [x] **Network filesystem detection** - Detect NFS/CIFS/SMB mounts by checking `/proc/mounts` on Linux, `GetVolumeInformation()` on Windows. Set `is_network_fs` flag in `ApprovedPath`. Optionally warn user about unreliable verification. See spec section "Path Resolution and TOCTOU Protection > Platform-Specific Considerations > Network Filesystems".

---

## User Prompt Interface

- [x] **Implement TTY approval prompt** - Display formatted prompt box showing tool name, arguments/command, and options: `[y] Allow`, `[n] Deny`, `[a] Allow always`, `[?] Details`. Read single keypress response. See spec section "User Prompt Interface".

- [x] **Implement details view** - When user presses `?`, display full arguments JSON and resolved paths. Return to prompt after display.

- [x] **Implement batch approval** - When multiple tool calls are pending, show numbered list of operations. Support `[y] Allow all`, `[n] Deny all`, `[1-N] Review individual`. See spec section "User Prompt Interface > Batch Approval".

- [x] **Handle Ctrl+C in prompt** - Return `APPROVAL_ABORTED` to signal workflow should stop without executing. See spec section "User Prompt Interface > Options" table.

---

## Allow Always Pattern Generation

- [x] **Implement file path pattern generation** - Extract directory component, generate pattern matching that directory and similar extensions. Root files get exact match. `/tmp` paths get exact match (security). See spec section "Allow Always Pattern Generation > Strategy" table for examples.

- [x] **Implement shell command pattern generation** - Add base command and first argument to allowlist. Never auto-allow commands with pipes, chains, or redirects. Generate `ShellAllowEntry` with command prefix. See spec section "Allow Always Pattern Generation > Strategy" for shell examples.

- [x] **Implement network URL pattern generation** - Parse URL, extract scheme + hostname, generate pattern that matches scheme, exact hostname, and requires path separator: `^https://api\.example\.com(/|$)`. Prevents subdomain spoofing. See spec section "Allow Always Pattern Generation > Strategy".

- [x] **Implement pattern confirmation dialog** - When generated pattern would match more than current operation, show confirmation with example matches. Offer `[y] Confirm`, `[e] Edit pattern`, `[x] Exact match only`. See spec section "Allow Always Pattern Generation > User Confirmation for Broad Patterns".

---

## Denial Rate Limiting

- [x] **Implement `DenialTracker` management** - Track per-tool denial count, last denial time, backoff expiry. Store in `ApprovalGateConfig.denial_trackers` array.

- [x] **Implement exponential backoff** - 1-2 denials: no backoff. 3: 5s. 4: 15s. 5: 60s. 6+: 5 minutes. See spec section "Denial Rate Limiting > Backoff Schedule".

- [x] **Implement `is_rate_limited()` check** - Return true if tool is in backoff period. Called before prompting user.

- [x] **Implement rate limit error response** - Return structured JSON error with `retry_after` field. See spec section "Denial Rate Limiting > Behavior During Backoff".

- [x] **Implement backoff reset** - Reset denial counter on: user approval, session restart, or backoff expiry without new denial. See spec section "Denial Rate Limiting".

---

## Subagent Approval Proxy

- [x] **Create `src/policy/subagent_approval.h`** - Define `ApprovalChannel` struct with request/response file descriptors and subagent PID. Define `ApprovalRequest` and `ApprovalResponse` structs for IPC messages. (Note: Core structs defined in approval_gate.h; helper functions for pipe management in subagent_approval.h)

- [x] **Create `src/policy/subagent_approval.c`** - Implement IPC-based approval proxying. Parent maintains exclusive TTY ownership, subagents send approval requests via pipe, parent prompts user and sends response. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Architecture: Approval Proxy".

- [x] **Implement `subagent_request_approval()`** - Subagent-side function that serializes request, writes to pipe, blocks waiting for response with timeout (5 minutes). Timeout results in denial. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Subagent Side".

- [x] **Implement `parent_approval_loop()`** - Parent-side multiplexing using `poll()` to monitor all subagent request pipes. Handle interleaved approvals from multiple concurrent subagents. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Parent Side".

- [x] **Implement `handle_subagent_approval_request()`** - Parent receives request, displays prompt (noting it's from subagent with PID), gets user response, sends back to subagent. If "Allow always", add pattern to parent's session allowlist. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Parent Side".

- [x] **Modify `spawn_subagent_with_approval()` in subagent_tool.c** - Create request/response pipes before fork. Child closes parent ends, sets up `ApprovalChannel`. Parent closes child ends, adds channel to monitoring set. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Spawn with Channels".

- [x] **Implement nested subagent approval forwarding** - When a subagent spawns its own subagent, requests are forwarded up the chain to root parent. Check `config->approval_channel` to determine if we're a subagent. See spec section "Subagent Behavior > Subagent Deadlock Prevention > Nested Subagents".

- [x] **Implement gate inheritance for subagents** - Subagents inherit parent's category config and static allowlist, but NOT session allowlist. Implement `approval_gate_init_from_parent()`. See spec section "Subagent Behavior > Gate Inheritance".

---

## Non-Interactive Mode

- [x] **Detect non-TTY stdin** - Check `isatty(STDIN_FILENO)` at startup. If false, set non-interactive mode. Implemented `approval_gate_detect_interactive()` and `approval_gate_is_interactive()` functions, called from main.c after `approval_gate_init()`.

- [x] **Implement non-interactive gate behavior** - Gated categories default to `deny` when no TTY available. Allowed categories proceed normally. Return structured error to LLM. See spec section "Non-Interactive Mode > Non-TTY Behavior". Added `APPROVAL_NON_INTERACTIVE_DENIED` result type and `format_non_interactive_error()` function. Modified `check_approval_gate()` and `check_approval_gate_batch()` to check `is_interactive` before prompting.

- [x] **Support partial trust flags** - `--allow-category=<category>` allows specific categories without prompting in non-interactive mode. `--allow="tool:args"` allows specific patterns. See spec section "Non-Interactive Mode > Partial Trust". Already implemented via existing CLI flag handling - setting a category to `GATE_ACTION_ALLOW` bypasses the gate check entirely, working in both interactive and non-interactive modes.

---

## Error Messages

- [x] **Implement structured denial error** - Return JSON with `error: "operation_denied"`, `message`, `tool`, `suggestion` field advising LLM to ask user or explain need. See spec section "Error Messages". Implemented in `format_denial_error()` in approval_gate.c.

- [x] **Implement protected file error** - Return JSON with `error: "protected_file"`, `message: "Cannot modify protected configuration file"`, `path`. See spec section "Protected Files". Implemented in `format_protected_file_error()` in approval_gate.c.

- [x] **Implement rate limit error** - Return JSON with `error: "rate_limited"`, `message`, `retry_after` seconds, `tool`. See spec section "Denial Rate Limiting > Behavior During Backoff". Implemented in `format_rate_limit_error()` in approval_gate.c.

- [x] **Implement path changed error** - Return JSON with `error: "path_changed"`, `message: "Path changed between approval and execution"` for TOCTOU violations (inode mismatch, parent changed). Also handles symlink rejection and other verification errors. Implemented in `format_verify_error()` in atomic_file.c.

---

## Integration with Tool Executor

- [x] **Modify `tool_executor.c` to check gates** - Before `execute_tool_call()`, check protected files first (hard block), then rate limiting, then approval gate. Handle all `ApprovalResult` cases. See spec section "Implementation > Integration Point" for code structure. Implemented `check_tool_approval()` helper function that checks protected files, rate limiting, and approval gates. Added to both `tool_executor_run_workflow()` and `tool_executor_run_loop()`. Modified `mk/sources.mk` to include `protected_files.c` and `path_normalize.c` in `RALPH_CORE_DEPS`.

- [ ] **Pass `ApprovedPath` to file tools** - When approval includes path verification, pass the `ApprovedPath` struct to file tools so they use `verify_and_open_approved_path()` instead of direct `open()`.

- [ ] **Add gate config to `RalphSession`** - Store `ApprovalGateConfig` in session structure. Initialize from config + CLI flags at session start.

- [ ] **Handle `APPROVAL_ABORTED`** - When user presses Ctrl+C during prompt, return from tool executor with abort status, don't process remaining tool calls.

---

## Unit Tests

- [x] **Create `test/test_approval_gate.c`** - Test gate config initialization, category lookup, non-interactive mode behavior, allowlist and rate limiter integration.

- [x] **Create `test/test_rate_limiter.c`** - Test opaque RateLimiter type: creation/destruction, denial tracking, backoff calculation, reset behavior, retry_after values. 18 comprehensive tests.

- [x] **Create `test/test_allowlist.c`** - Test opaque Allowlist type: creation/destruction, regex pattern addition/matching, shell allowlist entries, shell-type filtering, JSON loading. 12 comprehensive tests.

- [x] **Create `test/test_shell_parser.c`** - Test POSIX shell tokenization, quote handling, metacharacter detection, chain/pipe detection, dangerous pattern matching. Test edge cases: empty commands, only whitespace, unbalanced quotes.

- [x] **Create `test/test_shell_parser_cmd.c`** - Test cmd.exe parsing: `&` as separator, `%VAR%` detection, `^` escape, double-quote handling. Test dangerous patterns.

- [x] **Create `test/test_shell_parser_ps.c`** - Test PowerShell parsing: cmdlet detection (case-insensitive), `-EncodedCommand`, `$()` subexpressions, script blocks. Test all dangerous cmdlets from spec.

- [x] **Create `test/test_path_normalize.c`** - Test Windows backslash conversion, case handling, drive letter normalization (`C:` → `/c/`), UNC paths (`//server/share` → `/unc/server/share`), duplicate slash removal, trailing slash handling.

- [x] **Create `test/test_protected_files.c`** - Test basename matching, prefix matching (`.env.*`), glob patterns, inode-based detection. Test case sensitivity differences between platforms.

- [x] **Create `test/test_atomic_file.c`** - Test `O_NOFOLLOW` symlink rejection, `O_EXCL` existing file failure, `openat()` parent verification, inode mismatch detection. Create actual temp files/symlinks for realistic tests.

---

## Integration Tests

- [ ] **Create `test/test_approval_gate_integration.c`** - Test end-to-end approval flow with mock TTY input. Test batch approval, allow always with pattern generation, denial rate limiting across multiple calls.

- [x] **Create `test/test_subagent_approval.c`** - Test pipe creation, channel setup, cleanup functions, poll functions, and null safety. Note: Full fork/exec tests are excluded from valgrind per CLAUDE.md subagent test guidelines.

- [ ] **Add approval gate tests to existing tool tests** - Extend `test_tools_system.c` to verify gates are checked. Test that protected files are rejected. Test that denied operations return proper error JSON.

---

## Makefile Updates

- [x] **Add new source files to Makefile** - Added all `src/policy/*.c` files to appropriate build targets in `mk/sources.mk`. Includes: approval_gate.c, allowlist.c, rate_limiter.c, gate_prompter.c, pattern_generator.c, tool_args.c, shell_parser.c, shell_parser_cmd.c, shell_parser_ps.c, protected_files.c, path_normalize.c, atomic_file.c, subagent_approval.c.

- [x] **Add new test targets** - Added test executables for test_approval_gate, test_allowlist, test_rate_limiter, test_shell_parser, test_path_normalize, test_protected_files, test_atomic_file, test_subagent_approval in `mk/tests.mk`. Subagent approval tests excluded from valgrind per CLAUDE.md guidelines.

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
