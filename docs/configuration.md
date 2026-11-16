# Configuration

Scaffold uses a layered configuration system. Settings are resolved in priority order:

1. CLI flags (highest priority)
2. Local config file (`scaffold.config.json` in the current directory)
3. User config file (`~/.local/scaffold/config.json`)
4. Environment variables
5. Built-in defaults (lowest priority)

## Config file

Create a `scaffold.config.json` in your project directory or `~/.local/scaffold/config.json` for global settings:

```json
{
  "api_url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o",
  "openai_api_key": "sk-...",
  "context_window": 128000,
  "enable_streaming": true,
  "check_updates": true
}
```

### All config keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api_url` | string | `https://api.openai.com/v1/chat/completions` | LLM API endpoint |
| `model` | string | `gpt-5-mini-2025-08-07` | Default model |
| `openai_api_key` | string | | OpenAI API key |
| `anthropic_api_key` | string | | Anthropic API key |
| `openai_api_url` | string | | Custom OpenAI-compatible endpoint |
| `embedding_api_url` | string | | Embedding service endpoint (defaults to the OpenAI embeddings API) |
| `embedding_model` | string | | Model for embeddings |
| `system_prompt` | string | | Custom system prompt override |
| `context_window` | number | `8192` | Context window size in tokens |
| `tool_result_max_bytes` | number | `32768` | Cap on each tool result sent to the model; longer results keep head and tail around an omission marker |
| `max_tokens` | number | `-1` | Max output tokens (`-1` = unlimited) |
| `enable_streaming` | boolean | `true` | Enable real-time response streaming |
| `check_updates` | boolean | `true` | Check for updates on startup |
| `shell_max_timeout` | number | `300` | Upper bound in seconds on the `shell` tool's `timeout` |

#### API retry settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api_max_retries` | number | `3` | Number of retry attempts on failure |
| `api_retry_delay_ms` | number | `1000` | Initial retry delay in milliseconds |
| `api_backoff_factor` | number | `2.0` | Exponential backoff multiplier |

#### Orchestration settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `max_subagents` | number | `5` | Maximum concurrent subagents |
| `subagent_timeout` | number | `300` | Subagent timeout in seconds |
| `max_workers_per_goal` | number | `3` | Max workers per GOAP goal (1-20) |

#### Model tiers

Scaffold supports model tiers that map logical names to specific models. Configure them in the `models` section:

```json
{
  "models": {
    "simple": "o4-mini",
    "standard": "gpt-5-mini-2025-08-07",
    "high": "gpt-5.2-2025-12-11"
  }
}
```

| Tier | Default | Use case |
|------|---------|----------|
| `simple` | `o4-mini` | Fast, lightweight tasks |
| `standard` | `gpt-5-mini-2025-08-07` | Balanced capability/speed |
| `high` | `gpt-5.2-2025-12-11` | Most capable, complex reasoning |

## Environment variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key (overrides config file) |
| `ANTHROPIC_API_KEY` | Anthropic API key (overrides config file) |
| `EMBEDDING_API_URL` | Embedding service endpoint (overrides config file) |
| `EMBEDDING_MODEL` | Embedding model (overrides config file) |

Environment variables take precedence over config file values.

## Provider detection

Scaffold auto-detects your LLM provider from the API URL:

| URL pattern | Provider |
|-------------|----------|
| `api.anthropic.com` | Anthropic |
| `api.openai.com` | OpenAI |
| `chatgpt.com/backend-api/codex` | Codex (via `--login` OAuth) |
| `localhost:*` | Local AI (LM Studio, Ollama, etc.) |

### Using local models

Point scaffold at a local OpenAI-compatible server:

```json
{
  "api_url": "http://localhost:1234/v1/chat/completions",
  "model": "local-model-name"
}
```

This works with LM Studio, Ollama (with OpenAI compatibility), and any server implementing the OpenAI chat completions API.

### Using Anthropic

```json
{
  "api_url": "https://api.anthropic.com/v1/messages",
  "model": "claude-sonnet-4-20250514",
  "anthropic_api_key": "sk-ant-..."
}
```

## Approval gates

Scaffold controls tool execution through an approval gate system. By default, potentially destructive operations require user confirmation.

### Categories

Each tool belongs to a category with a default action:

| Category | Default | Tools |
|----------|---------|-------|
| `file_read` | allow | `read_file`, `file_info`, `list_dir`, `search_files` |
| `file_write` | gate | `write_file`, `append_file`, `apply_patch` |
| `shell` | gate | `shell` |
| `network` | gate | `web_fetch` |
| `memory` | allow | `remember`, `recall_memories`, vector DB, messaging |
| `subagent` | gate | `subagent` |
| `mcp` | gate | MCP tool calls |
| `python` | gate | `python`, and tools with no declared category (`--allow-category=python` allows them) |
| `plugin` | gate | Plugin tool calls |

Actions:
- **allow** -- Execute automatically without prompting
- **gate** -- Require user approval before execution
- **deny** -- Block execution entirely

### Configuration

Configure gates in your config file:

```json
{
  "approval_gates": {
    "enabled": true,
    "auto_mode": false,
    "categories": {
      "file_write": "gate",
      "shell": "gate",
      "network": "allow"
    },
    "allowlist": [
      {
        "tool": "write_file",
        "pattern": "^/tmp/.*\\.txt$"
      },
      {
        "tool": "shell",
        "command": ["git", "status"]
      }
    ]
  }
}
```

### Allowlist entries

**Regex patterns** (for file and network tools), matched against the call's path or URL:
```json
{"tool": "write_file", "pattern": "^src/.*\\.js$"}
```

**Shell command patterns** (prefix matching):
```json
{"tool": "shell", "command": ["npm", "install"]}
```

Shell patterns can optionally specify a shell type (`bash`, `sh`, `cmd`, `powershell`):
```json
{"tool": "shell", "command": ["git", "push"], "shell": "bash"}
```

A POSIX command chained with `;`, `&&`, `||`, `|` or newlines matches only when every segment matches an entry, so `cd src && make 2>&1 | tail` needs entries for `cd`, `make` and `tail`. `2>&1` and redirects to `/dev/null` are ignored. Segments with other redirects, subshells (`$(...)`, backticks), backslash escapes or a background `&` never match, and neither do commands that match a dangerous pattern such as `rm -rf`.

A call whose arguments JSON repeats a key at any nesting level is denied before any of this: scaffold reads the first value of a duplicated key and the tool executor reads the last, so `{"command": "git status", "command": "touch x"}` would be checked as one command and run as another. Names are compared case-insensitively.

### CLI overrides

```bash
# Disable all gates for this session
./scaffold --yolo

# Allow a specific tool pattern
./scaffold --allow "write_file:^src/.*"

# Allow an entire category
./scaffold --allow-category=network

# Run gated calls that pass auto review without prompting
./scaffold --auto
```

### Auto mode

Gated shell calls always prompt in normal mode, however harmless they look; the agent has approval-free read tools for reading files. With `--auto` they go to the model review below.

With `--auto` or `"auto_mode": true`, a gated call that matches no allowlist entry is reviewed before scaffold prompts. Shell calls skip allowlist matching altogether in this mode -- config entries, `--allow` entries and rules saved with `a` are all ignored -- because what a command does depends on files the agent may just have written, which the review sees and an entry doesn't:

1. `write_file` and `apply_patch` calls whose every path stays inside the git project containing the working directory, outside `.git` and protected files, run.
2. Anything else goes to the session's own model in a separate request that is not added to the conversation. The reviewer sees your recent messages and the pending call, never tool output. If it allows the call, the call runs.
3. Otherwise you get the normal prompt, preceded by the reviewer's reason. Without a terminal, the call is denied.

A failed review request or a reply that isn't a JSON verdict falls through to the prompt. Each reviewed call costs one extra model request, which is slow on local models.

### Approval prompt

When a gate triggers, you'll see:

```
[GATE] shell: git show HEAD
Allow? [y/n/?] (a = always allow "git show HEAD")
```

When the call runs somewhere else, the detail line names that directory: `shell: make test (in /tmp/x)`. Control characters in a command, a path or the arguments are shown as escapes (`\n`, `\x1b`) in the prompt, the details view and the batch preview.

- **y** -- Allow this one action
- **n** -- Deny this action
- **a** -- Allow this and all matching future actions (session only), and run the current one. For a shell command, each segment that can have a rule gets one; the prompt names the rules it would save, and a partial save reports what happened, e.g. `Saved: "grep -rn x lib". Still asks: git fetch (writes to a file).` When no part of the command can be saved, `a` isn't offered and does nothing.
- **?** -- Show details about the action

A shell rule saved by `a` is the exact command of that segment. It covers only the same command again: whitespace and quoting don't matter, anything else does. `a` on `git status` does not cover `git status -sb`, `grep -rn foo lib` does not cover `grep -nr foo lib`, and `python3 x.py` covers only `python3 x.py`.

A rule also records the directory the call ran in -- the shell tool's `working_dir` argument resolved against the current directory, or the current directory when the call has none -- and matches only calls that resolve to the same directory, so a rule for `make test` in the project doesn't cover `make test` in a downloaded repository. When that directory isn't the current one, the prompt names it: `a = always allow "make test" in /tmp/x`.

What can't be saved at all:

- A command the shell would rewrite before running it: a substitution (`$(...)`, backticks), a variable (`$HOME`, `${VAR:-x}`), a glob (`cat *.txt`, `ls dir?`) or a brace expansion (`cat .{a,b}`).
- A chain, a pipe that can't be split, or a redirect that writes a file.
- A command whose text this parser can't read the way the shell would: a backslash outside single quotes, a comment (`#` starting a word), a heredoc (`<<`), or a newline, `\r`, `\v` or `\f` outside single quotes. Such a command matches no entry either, config or session.
- A token with a `~` the shell replaces with a home directory: at the start of the token, or after an `=` or `:` in it (`make PREFIX=~/x`).
- `cd`, `pushd` and `popd`, which move the directory the segments after them run in. A command containing such a segment also matches no rule saved by `a`, so a saved `make` can't be reached with `cd .. && make`.
- Any command with a segment this parser can't read: when one segment is refused for that reason, no segment of the command is saved.
- A flag that changes what the command does, including an abbreviated long option, which is accepted for every command (`git push --forc`, `rm --rec`, `tar --to-com`): `git push --force`, `git push origin +main`, `git push --mirror`, `git branch -d`, `git tag -f`, `git clean --force`, `sed -i`, `sed --in-place`, `perl -i`, `rsync --delete`, `find -delete`, `rm -r`, `curl -o`, `wget -O`, `tar --to-command`, `make -f`, `--output`. The flags are looked up on the command behind any `VAR=value` prefix and any wrapper that runs it (`sudo`, `doas`, `env`, `command`, `nice`, `nohup`, `timeout`, `time`, `stdbuf`, `setsid`, `ionice`, `xargs`, `exec`, `chroot`, `flock`, `watch`, `strace`, `script`), so `sudo git push --force` and `A=1 git push -f` can't be saved; a wrapper option scaffold can't classify (`sudo -i`) makes the segment unsaveable too.
- An operand that names a protected file, directly (`cat .env`), through a pattern (`cat .en?`) or after a revision (`git show HEAD:.env`). A saved rule is checked against this too.

Everything else is saved as typed, including interpreters, wrappers, `VAR=value` prefixes and absolute paths — the rule still only ever covers that one command.

Entries from the config file and `--allow` are not rules: they keep token prefix matching, so `["git", "status"]` covers `git status -sb`. They apply to any directory, and `--auto` ignores them as described above.

For batch operations (multiple tool calls at once), you can approve/deny all or select individual items by number.

### Protected files

Certain files are always blocked regardless of gate settings. These include sensitive configuration files like `.env`, credentials, and key files. This is a hard security boundary that cannot be overridden.

### Subagent behavior

Subagents inherit category settings and static allowlist entries from the parent config. Session-added allowlist entries (from `--allow` or "always allow" prompts) are not inherited; instead, a subagent's gated call goes to the parent, which checks it against its own allowlist and auto mode before prompting.

A subagent is started with the parent's `--auto`, so a shell call in a subagent skips the child's own allowlist too and reaches the parent's review. Every subagent is also started with the parent's home directory, so it reads the same `config.json` the parent did.

Goal supervisors and GOAP workers follow the same rule. A child is started with `--yolo` if the parent runs with `--yolo`, with `--auto` if the parent runs with `--auto`, and with neither otherwise; each also gets the parent's `--home`. A supervisor passes its own mode on to its workers. A supervisor has no terminal, so without `--auto` or `--yolo` a shell or file-write call that it or a worker makes is refused, and the refusal says background goals need `--auto` (or `--yolo`).

## Project-specific prompts (AGENTS.md)

Scaffold reads an `AGENTS.md` file from the current directory and appends it to the system prompt. Use this to give scaffold project-specific context:

```markdown
# Project: My App

## Stack
- Frontend: React with TypeScript
- Backend: Express with PostgreSQL
- Testing: Jest

## Conventions
- Use functional components with hooks
- API responses follow JSON:API format
- All endpoints need input validation
```

### File references

Reference other files in your `AGENTS.md` with `@filename` syntax:

```markdown
See @ARCHITECTURE.md for system design.
Review @API_SPEC.md for endpoint definitions.
```

Referenced files are expanded inline and wrapped in XML tags. Each file is included only once (deduplicated).
