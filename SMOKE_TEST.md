# Scaffold Manual Smoke Test Plan

## Prerequisites

1. **Build passes**: `./scripts/build.sh && ./scripts/run_tests.sh`
2. **Codex authentication**: Run `out/scaffold --login` and complete the OAuth flow. Verify `~/.local/scaffold/oauth2.db` exists and contains valid tokens.
3. **Default app home**: `~/.local/scaffold/` has valid OAuth tokens. This directory is shared across all tests.

---

## Test Harness

Every test runs in an isolated temporary directory. Follow this setup/teardown procedure for **each test section**:

```bash
# --- Setup ---
WORKDIR=$(mktemp -d /tmp/scaffold-smoke.XXXXXX)
cp out/scaffold "$WORKDIR/scaffold"
cp .env "$WORKDIR/.env"
cd "$WORKDIR"
source .env

# --- Run test steps (use ./scaffold) ---

# --- Teardown ---
cd -
rm -rf "$WORKDIR"
```

- Each test gets a **fresh working directory** — no leftover files from prior tests
- The binary and `.env` are copied in; `.env` is sourced for environment variables
- The default app home `~/.local/scaffold/` is **shared** (contains OAuth tokens, memory, config)
- Teardown removes the temp dir after each test

---

## P0 — Core Functionality (Must Pass)

### 1. Fresh Start Experience

- [ ] Remove `~/.local/scaffold/` entirely
- [ ] Run `./scaffold --version` — should bootstrap `~/.local/scaffold/` and subdirectories
- [ ] Verify `~/.local/scaffold/tools/` has built-in Python tools extracted
- [ ] Re-run `out/scaffold --login` to restore OAuth tokens before continuing

### 2. Interactive Mode — Basic Conversation

- [ ] Run `./scaffold` — REPL prompt appears, status line renders
- [ ] Send a simple message ("What is 2+2?") — streamed response appears
- [ ] Send a follow-up requiring context — model remembers prior turn
- [ ] `Ctrl+C` during streaming — cancels cleanly, returns to prompt
- [ ] `quit` or `Ctrl+D` — exits cleanly with no crash

### 3. Single-Shot Mode

- [ ] `./scaffold "What is the capital of France?"` — responds and exits with code 0
- [ ] Verify no interactive prompt appears

### 4. Piped Input

- [ ] `echo "List 3 colors" | ./scaffold` — processes input, responds, exits

### 5. Tool Use — File Operations

- [ ] Ask scaffold to read a file ("read the .env file") — **blocked** (hard-blocked, no prompt)
- [ ] Ask scaffold to read a non-protected file (create one first) — approval gate fires (`file_read`)
- [ ] Ask scaffold to create a small test file — approval gate fires (`file_write`)
- [ ] Press `y` to approve — file is written correctly
- [ ] Press `n` on next write request — tool is denied, agent adapts
- [ ] Press `a` (always) on next request — pattern added to session allowlist

### 6. Tool Use — Shell

- [ ] Ask scaffold to run `ls` — approval gate fires (`shell`)
- [ ] Approve — output is correct
- [ ] Ask to run a multi-command pipeline — verify correct output

### 7. Approval Gates — `--yolo`

- [ ] `./scaffold --yolo "Create a file called /tmp/smoke_yolo.txt with 'hello'"` — no approval prompts, file is created
- [ ] Verify `/tmp/smoke_yolo.txt` exists and contains "hello"

---

## P1 — Important Features

### 8. Model Switching

- [ ] `/model list` — shows available tiers
- [ ] `/model high` — switches to high-tier model
- [ ] Send a message — confirm it uses the new model (check with `--debug` if needed)
- [ ] `./scaffold --model gpt-4o "Hello"` — CLI flag overrides config

### 9. Behavioral Modes

- [ ] `/mode list` — shows all modes
- [ ] `/mode plan` — switches mode, system prompt changes
- [ ] `/mode explore` — switches, verify read-only behavior emphasis
- [ ] `/mode default` — returns to normal

### 10. Slash Commands

- [ ] `/help` — prints help text
- [ ] `/goals` — shows goal tree (empty is fine)
- [ ] `/tasks list` — shows task queue
- [ ] `/agents list` — shows running agents
- [ ] `/memory stats` — shows memory statistics
- [ ] `/memory list` — lists memories (empty is fine on fresh install)
- [ ] `/plugins` — shows loaded plugins (empty list is fine)

### 11. Streaming vs Non-Streaming

- [ ] Default run — text streams in real-time (visually verify)
- [ ] `./scaffold --no-stream "Tell me a joke"` — response appears all at once

### 12. Debug Output

- [ ] `./scaffold --debug "Hello"` — HTTP request/response traffic visible
- [ ] Verify API key is NOT shown in debug output

### 13. Memory System

- [ ] Ask scaffold to remember something: "Remember that my favorite color is blue"
- [ ] Exit and restart scaffold (new workdir, same `~/.local/scaffold/`)
- [ ] Ask "What is my favorite color?" — should recall from memory
- [ ] `/memory search blue` — should find the memory
- [ ] `/memory list` — memory entry visible

### 14. Version & Help

- [ ] `./scaffold --version` — prints version string with git hash
- [ ] `./scaffold --help` — prints usage info, exits cleanly

### 15. OAuth Login / Logout

- [ ] `./scaffold --login` — opens browser or prints OAuth URL, completes flow, stores tokens
- [ ] Verify `~/.local/scaffold/oauth2.db` exists and is non-empty
- [ ] Send a message — response comes back via Codex provider
- [ ] `./scaffold --logout` — clears OAuth tokens
- [ ] Verify `~/.local/scaffold/oauth2.db` is removed or tokens are cleared
- [ ] Re-run `out/scaffold --login` to restore tokens before continuing

### 16. Codex Provider

- [ ] After successful `--login`, run `./scaffold "What is 2+2?"` with no `OPENAI_API_KEY` set
- [ ] Verify response arrives via Codex endpoint (use `--debug` to confirm URL)
- [ ] Confirm model selection works with Codex (`--model` flag still applies)

### 17. `--allow` Spec

- [ ] `./scaffold --allow "shell:ls"` — start interactive mode
- [ ] Ask scaffold to run `ls` — auto-approved, no gate prompt
- [ ] Ask scaffold to run `cat somefile` — approval gate **fires** (not covered by spec)
- [ ] Exit cleanly

### 18. `--allow-category`

- [ ] `./scaffold --allow-category=shell` — start interactive mode
- [ ] Ask scaffold to run `ls` — auto-approved
- [ ] Ask scaffold to run `cat somefile` — also auto-approved (whole category allowed)
- [ ] Ask scaffold to write a file — approval gate **fires** (`file_write` not allowed)
- [ ] Exit cleanly

---

## P2 — Advanced Features

### 19. Subagent Spawning

- [ ] Ask scaffold to do something complex ("Analyze this project's architecture and write a summary")
- [ ] Approval gate fires for subagent — approve
- [ ] Subagent runs, result is incorporated into response
- [ ] `/agents list` during execution — shows running subagent

### 20. GOAP Orchestration

- [ ] Give a multi-step task ("Create a Python script that fetches weather data, write tests for it, and review the code")
- [ ] Verify goals and actions are created (`/goals`)
- [ ] Observe supervisor/worker lifecycle
- [ ] `/tasks list` — shows work items

### 21. Todo Tool

- [ ] Ask scaffold to create a todo list for a task
- [ ] Verify TodoWrite and TodoRead tools are called
- [ ] Ask to check off items

### 22. Python Tool

- [ ] Ask scaffold to run Python code ("Use Python to calculate the first 10 Fibonacci numbers")
- [ ] Verify Python interpreter executes correctly
- [ ] Verify output is returned to the conversation

### 23. Web Fetch

- [ ] Ask scaffold to fetch a URL ("Fetch https://httpbin.org/get and summarize it")
- [ ] Approval gate fires for network category — approve
- [ ] Verify content is retrieved and summarized

### 24. Protected Files

- [ ] Ask scaffold to read `.env` — should be blocked (hard-blocked, no prompt)
- [ ] Ask scaffold to read `scaffold.config.json` — should be blocked
- [ ] Verify error message is clear

### 25. Image Attachment

- [ ] Create a simple PNG/JPEG test image
- [ ] Send `@/path/to/image.png describe this image` — verify image is sent to LLM
- [ ] Verify response describes the image (requires vision-capable model)

### 26. Configuration Cascade

- [ ] Set a custom model in `~/.local/scaffold/config.json`
- [ ] Create a local `scaffold.config.json` in the workdir with a different model
- [ ] Verify local config wins over user config
- [ ] Pass `--model` on CLI — verify CLI wins over both

### 27. MCP Server Integration

- [ ] Configure a simple MCP server in `scaffold.config.json` (e.g., a filesystem MCP server)
- [ ] Start scaffold — verify MCP server starts, tools are registered
- [ ] Use an MCP tool — approval gate fires (`mcp`), tool executes
- [ ] Remove MCP config — verify graceful degradation (no crash)

### 28. Custom Python Tools

- [ ] Create a custom `.py` tool in `~/.local/scaffold/tools/`
- [ ] Restart scaffold — tool should be discovered and registered
- [ ] Ask scaffold to use the custom tool — verify execution

### 29. JSON Output Mode

- [ ] `./scaffold --json "What is 2+2?"` — output is valid JSON
- [ ] Parse the JSON — verify structure contains response content

### 30. `--system-prompt-file`

- [ ] Create a temp file: `echo "You are a pirate. Always respond in pirate speak." > /tmp/pirate.txt`
- [ ] `./scaffold --system-prompt-file /tmp/pirate.txt "What is 2+2?"` — response uses pirate theme
- [ ] Verify the custom system prompt overrides default behavior

### 31. Invalid Model

- [ ] `./scaffold --model nonexistent-model-xyz "Hello"` — verify clear error message
- [ ] Verify non-zero exit code (`echo $?` returns 1)

### 32. Missing Credentials

- [ ] In a fresh workdir, do **not** source `.env`
- [ ] Temporarily move `~/.local/scaffold/oauth2.db` aside
- [ ] `./scaffold "Hello"` — verify clear error message about missing credentials
- [ ] Restore `oauth2.db` before continuing

### 33. `/plugins` Command

- [ ] `/plugins` in interactive mode — shows loaded plugins (empty list is acceptable)
- [ ] If plugins are configured, verify they appear in the list

---

## P3 — Edge Cases & Robustness

### 34. Large Context Handling

- [ ] Feed a very large file (>1000 lines) and ask questions about it
- [ ] Verify conversation compaction works on long sessions (many turns)

### 35. Network Resilience

- [ ] Temporarily break the API URL in config — verify clear error message
- [ ] Verify retry logic works (briefly disconnect network during request)

### 36. Concurrent Operations

- [ ] Start multiple scaffold instances in different temp directories
- [ ] Verify no SQLite lock conflicts or data corruption

### 37. Signal Handling

- [ ] `Ctrl+C` during tool execution — cancels cleanly
- [ ] `Ctrl+C` twice rapidly — exits without crash
- [ ] `Ctrl+D` on empty prompt — exits cleanly

### 38. Update System

- [ ] `./scaffold --check-update` — reports current version and whether update is available
- [ ] (Caution) `./scaffold --update` — downloads and replaces binary if update available. **Only run this if you intend to update.**

### 39. AGENTS.md Project Context

- [ ] Create an `AGENTS.md` in the workdir with project-specific instructions
- [ ] Start scaffold — verify context is appended to system prompt (observable via `--debug`)
- [ ] Use `@filename` syntax in AGENTS.md — verify referenced file is included

### 40. Allowlist Patterns (Advanced)

- [ ] `./scaffold --allow "write_file:^/tmp/.*"` — file writes under `/tmp/` auto-approved, others gated
- [ ] `./scaffold --allow "shell:npm,install"` — `npm install` auto-approved, other shell commands gated
- [ ] `./scaffold --allow-category=file_read --allow-category=network` — both categories auto-approved

### 41. Exit Codes

- [ ] `./scaffold "Hello"` — exits with code 0 (`echo $?`)
- [ ] `./scaffold --model nonexistent "Hello"` — exits with code 1
- [ ] `./scaffold --badoption` — exits with code 1

### 42. `--no-auto-messages`

- [ ] `./scaffold --no-auto-messages` — start interactive mode
- [ ] Verify no automatic message polling occurs (observable via `--debug`)
- [ ] Manual conversation still works normally

### 43. `--home` Override

- [ ] `./scaffold --home /tmp/scaffold-alt "Hello"` — uses `/tmp/scaffold-alt` as app home
- [ ] Verify `/tmp/scaffold-alt/` is created with expected subdirectories
- [ ] Clean up `/tmp/scaffold-alt/` after test

---

## Coverage Checklist

### CLI Flags

| Flag | Test # |
|------|--------|
| `--help` | 14 |
| `--version` | 14 |
| `--debug` | 12 |
| `--no-stream` | 11 |
| `--json` | 29 |
| `--home` | 43 |
| `--model` | 8, 31 |
| `--system-prompt-file` | 30 |
| `--yolo` | 7 |
| `--allow` | 17, 40 |
| `--allow-category` | 18, 40 |
| `--login` | 15 |
| `--logout` | 15 |
| `--check-update` | 38 |
| `--update` | 38 |
| `--no-auto-messages` | 42 |

### Slash Commands

| Command | Test # |
|---------|--------|
| `/help` | 10 |
| `/goals` | 10, 20 |
| `/tasks list` | 10, 20 |
| `/agents list` | 10, 19 |
| `/memory stats` | 10 |
| `/memory list` | 10, 13 |
| `/memory search` | 13 |
| `/model list` | 8 |
| `/model <tier>` | 8 |
| `/mode list` | 9 |
| `/mode <name>` | 9 |
| `/plugins` | 10, 33 |

### Approval Gate Categories

| Category | Test # |
|----------|--------|
| `file_read` | 5, 40 |
| `file_write` | 5, 18, 40 |
| `shell` | 6, 17, 18 |
| `network` | 23 |
| `memory` | 13 |
| `subagent` | 19 |
| `mcp` | 27 |
| `python` | 22 |
| `plugin` | 28 |
