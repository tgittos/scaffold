#ifndef SHELL_PARSER_H
#define SHELL_PARSER_H

/**
 * Cross-Platform Shell Command Parser
 *
 * Parses shell commands to detect dangerous patterns, command chaining,
 * and to enable secure allowlist matching. Supports:
 * - POSIX shells (bash, sh, zsh, dash)
 * - Windows cmd.exe
 * - PowerShell (Windows and Core)
 *
 * The parser is intentionally conservative: commands containing any potentially
 * dangerous constructs (pipes, chains, subshells, etc.) are flagged and never
 * auto-matched by allowlist entries.
 *
 * Security considerations:
 * - Commands with chain operators (;, &&, ||, &) never match allowlist
 * - Commands with pipes (|) never match allowlist
 * - Commands with subshells ($(), ``) never match allowlist
 * - Dangerous patterns (rm -rf, fork bombs, etc.) are always flagged
 * - ANSI-C quoting ($'...') is detected and prevents allowlist matching
 * - Non-ASCII characters are flagged (potential Unicode lookalike attacks)
 * - Backslash escapes are flagged (complex parsing, potential bypasses)
 * - Unbalanced quotes are flagged as unsafe for matching
 *
 * Related headers:
 * - approval_gate.h: Uses parsed commands for allowlist matching
 * - protected_files.h: Protected file detection for shell commands
 */

/* ============================================================================
 * Shell Type Detection
 * ========================================================================== */

/**
 * Shell types for cross-platform parsing.
 * Each shell type has different metacharacters and parsing rules.
 */
typedef enum {
    SHELL_TYPE_POSIX,       /* bash, sh, zsh, dash - uses ; && || | $() `` */
    SHELL_TYPE_CMD,         /* Windows cmd.exe - uses & && || | %VAR% */
    SHELL_TYPE_POWERSHELL,  /* PowerShell (Windows or Core) - uses ; && || | $() {} */
    SHELL_TYPE_UNKNOWN      /* Unable to detect, treated as POSIX */
} ShellType;

/* ============================================================================
 * Parsed Command Structure
 * ========================================================================== */

/**
 * Result of parsing a shell command.
 *
 * The parser extracts tokens and detects potentially dangerous constructs.
 * Commands with any flag set (has_chain, has_pipe, etc.) should never be
 * auto-matched by allowlist entries.
 */
typedef struct {
    /* Tokenized command */
    char **tokens;          /* Array of command tokens (allocated) */
    int token_count;        /* Number of tokens */

    /* Detected constructs - any of these prevents allowlist matching */
    int has_chain;          /* Contains ; && || (POSIX/PS) or & && || (cmd) */
    int has_pipe;           /* Contains | */
    int has_subshell;       /* Contains $() or `` (POSIX/PS) */
    int has_redirect;       /* Contains > >> < << */

    /* Security flags */
    int is_dangerous;       /* Matches dangerous pattern (rm -rf, etc.) */

    /* Shell context */
    ShellType shell_type;   /* Shell used for parsing */
} ParsedShellCommand;

/* ============================================================================
 * Shell Detection
 * ========================================================================== */

/**
 * Detect the shell type from the environment.
 *
 * Detection strategy:
 * - Windows: Check COMSPEC for cmd.exe, PSModulePath for PowerShell
 * - POSIX: Check SHELL for pwsh/powershell, default to POSIX
 *
 * @return Detected shell type, or SHELL_TYPE_POSIX as fallback
 */
ShellType detect_shell_type(void);

/**
 * Get the name of a shell type as a string.
 *
 * @param type The shell type
 * @return Static string name (e.g., "posix", "cmd", "powershell")
 */
const char *shell_type_name(ShellType type);

/**
 * Parse a shell type from a string name.
 *
 * @param name Shell type name (case-insensitive)
 * @param out_type Output: the parsed shell type
 * @return 0 on success, -1 if name not recognized
 */
int parse_shell_type(const char *name, ShellType *out_type);

/* ============================================================================
 * Command Parsing - Unified Interface
 * ========================================================================== */

/**
 * Parse a shell command using the detected shell type.
 *
 * Automatically detects the shell type and applies appropriate parsing rules.
 * For explicit shell type, use parse_shell_command_for_type().
 *
 * @param command The raw command string
 * @return Allocated ParsedShellCommand, or NULL on error.
 *         Caller must free with free_parsed_shell_command().
 */
ParsedShellCommand *parse_shell_command(const char *command);

/**
 * Parse a shell command using a specific shell type.
 *
 * @param command The raw command string
 * @param type Shell type to use for parsing
 * @return Allocated ParsedShellCommand, or NULL on error.
 *         Caller must free with free_parsed_shell_command().
 */
ParsedShellCommand *parse_shell_command_for_type(const char *command,
                                                  ShellType type);

/**
 * Free a parsed shell command.
 *
 * @param cmd Command to free (NULL safe)
 */
void free_parsed_shell_command(ParsedShellCommand *cmd);

/* ============================================================================
 * Dangerous Pattern Detection
 * ========================================================================== */

/**
 * Check if a command contains known dangerous patterns.
 *
 * These patterns always require approval regardless of allowlist:
 * - rm -rf, rm -fr (recursive forced deletion)
 * - > /dev/sd* (direct disk write)
 * - dd if=* of=/dev/... (disk overwrite)
 * - chmod 777, chmod -R (broad permission changes)
 * - curl | sh, wget | sh (remote code execution)
 * - Fork bomb patterns
 *
 * This check is performed against the raw command string before parsing.
 *
 * @param command Raw command string
 * @return 1 if dangerous pattern detected, 0 otherwise
 */
int shell_command_is_dangerous(const char *command);

/**
 * Check if a PowerShell command contains dangerous cmdlets.
 *
 * Dangerous cmdlets include:
 * - Invoke-Expression (iex)
 * - Invoke-Command (icm)
 * - Start-Process
 * - -EncodedCommand (-enc)
 * - DownloadString/DownloadFile
 *
 * Case-insensitive matching is used.
 *
 * @param command Raw PowerShell command string
 * @return 1 if dangerous cmdlet detected, 0 otherwise
 */
int powershell_command_is_dangerous(const char *command);

/* ============================================================================
 * Allowlist Matching Support
 * ========================================================================== */

/**
 * Check if a parsed command matches an allowlist prefix.
 *
 * Matching rules:
 * 1. Parsed tokens must start with the prefix tokens
 * 2. Commands with chains/pipes/subshells NEVER match
 * 3. Dangerous commands NEVER match
 *
 * Example: prefix ["git", "status"] matches "git status -s" but not
 * "git status; rm -rf /" (has chain).
 *
 * @param parsed The parsed command
 * @param prefix Array of prefix tokens to match (const, not modified)
 * @param prefix_len Number of tokens in prefix
 * @return 1 if matches, 0 if not
 */
int shell_command_matches_prefix(const ParsedShellCommand *parsed,
                                 const char * const *prefix,
                                 int prefix_len);

/**
 * Check if two commands are cross-platform equivalents.
 *
 * Recognizes equivalents like:
 * - ls <-> dir <-> Get-ChildItem <-> gci
 * - cat <-> type <-> Get-Content <-> gc
 * - rm <-> del <-> Remove-Item <-> ri
 *
 * Shell types are used to ensure equivalence is appropriate for the context.
 * For example, 'ls' on POSIX is equivalent to 'dir' on cmd.exe.
 *
 * @param allowed_cmd Command name from allowlist entry
 * @param actual_cmd Command name from parsed command
 * @param allowed_shell Shell type of the allowlist entry (or UNKNOWN for any)
 * @param actual_shell Shell type of the actual command
 * @return 1 if equivalent, 0 if not
 */
int commands_are_equivalent(const char *allowed_cmd,
                            const char *actual_cmd,
                            ShellType allowed_shell,
                            ShellType actual_shell);

/* ============================================================================
 * POSIX Shell Parsing (to be implemented in shell_parser.c)
 * ========================================================================== */

/**
 * Parse a POSIX shell command.
 *
 * Parsing rules:
 * - Tokenize on unquoted whitespace
 * - Respect single and double quotes (no escape in single quotes)
 * - Detect metacharacters: ; | & ( ) $ ` > <
 * - Mark as dangerous if metacharacter appears unquoted
 *
 * @param command Raw command string
 * @param result Pre-allocated structure to fill
 * @return 0 on success, -1 on error
 */
int parse_posix_shell(const char *command, ParsedShellCommand *result);

/* ============================================================================
 * cmd.exe Parsing (to be implemented in shell_parser.c)
 * ========================================================================== */

/**
 * Parse a Windows cmd.exe command.
 *
 * Parsing rules:
 * - Only double quotes are string delimiters
 * - Detect metacharacters: & | < > ^ %
 * - & is unconditional separator (like ; in POSIX)
 * - ^ is escape character
 * - %VAR% is variable expansion
 *
 * @param command Raw command string
 * @param result Pre-allocated structure to fill
 * @return 0 on success, -1 on error
 */
int parse_cmd_shell(const char *command, ParsedShellCommand *result);

/* ============================================================================
 * PowerShell Parsing (to be implemented in shell_parser.c)
 * ========================================================================== */

/**
 * Parse a PowerShell command.
 *
 * Parsing rules:
 * - Similar to POSIX but with additional constructs
 * - Detect ; && || | for chaining
 * - Detect $() for subexpressions
 * - Detect {} for script blocks
 * - Detect & and . as call operators
 * - Check for dangerous cmdlets
 *
 * @param command Raw command string
 * @param result Pre-allocated structure to fill
 * @return 0 on success, -1 on error
 */
int parse_powershell(const char *command, ParsedShellCommand *result);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * Check if a command can be safely matched against allowlist.
 *
 * Returns false if the command has any constructs that prevent safe matching:
 * - Chain operators
 * - Pipes
 * - Subshells
 * - Redirects
 * - Dangerous patterns
 *
 * @param parsed The parsed command
 * @return 1 if safe for matching, 0 if not
 */
int shell_command_is_safe_for_matching(const ParsedShellCommand *parsed);

/**
 * Get the base command from a parsed command.
 *
 * @param parsed The parsed command
 * @return First token (base command), or NULL if no tokens
 */
const char *shell_command_get_base(const ParsedShellCommand *parsed);

/**
 * Create a copy of a parsed command.
 *
 * @param cmd Command to copy
 * @return Allocated copy, or NULL on error. Caller must free.
 */
ParsedShellCommand *copy_parsed_shell_command(const ParsedShellCommand *cmd);

#endif /* SHELL_PARSER_H */
