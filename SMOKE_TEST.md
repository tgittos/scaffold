# Scaffold Manual Smoke Test Plan

## Prerequisites

- Build passes: `./scripts/build.sh && ./scripts/run_tests.sh`
- `.env` has valid API credentials (OpenAI key at minimum)
- Clean state: remove `~/.local/scaffold/` to test fresh install experience

---

## P0 - Core Functionality (Must Pass)

### 1. Fresh Start Experience

- [ ] Run `./scaffold` with no `~/.local/scaffold/` directory — should bootstrap cleanly
- [ ] Verify `~/.local/scaffold/` and subdirectories are created
- [ ] Verify built-in Python tools are extracted to `~/.local/scaffold/tools/`

### 2. Interactive Mode - Basic Conversation

- [ ] Run `./scaffold` — REPL prompt appears, status line renders
- [ ] Send a simple message ("What is 2+2?") — streamed response appears
- [ ] Send a follow-up that requires conversation context — model remembers prior turn
- [ ] `Ctrl+C` during streaming — cancels cleanly, returns to prompt
- [ ] `quit` or `Ctrl+D` — exits cleanly with no crash

### 3. Single-Shot Mode

- [ ] `./scaffold "What is the capital of France?"` — responds and exits with code 0
- [ ] Verify no interactive prompt appears

### 4. Piped Input

- [ ] `echo "List 3 colors" | ./scaffold` — processes input, responds, exits

### 5. Tool Use - File Operations

- [ ] Ask scaffold to read a file in the project ("read the Makefile")
- [ ] Ask scaffold to create a small test file — approval gate fires
- [ ] Press `y` to approve — file is written correctly
- [ ] Press `n` on next write request — tool is denied, agent adapts
- [ ] Press `a` (always) — pattern is added to session allowlist

### 6. Tool Use - Shell

- [ ] Ask scaffold to run `ls` — approval gate fires for shell category
- [ ] Approve — output is correct
- [ ] Ask to run a multi-command pipeline — verify correct output

### 7. Approval Gates - `--yolo`

- [ ] `./scaffold --yolo "Create a file called /tmp/smoke_test.txt with 'hello'"` — no approval prompts, file is created

---

## P1 - Important Features

### 8. Model Switching

- [ ] `/model list` — shows available tiers
- [ ] `/model high` — switches to high-tier model
- [ ] Send a message — confirm it uses the new model (check with `--debug` if needed)
- [ ] `--model gpt-4o` on CLI — overrides config

### 9. Behavioral Modes

- [ ] `/mode list` — shows all modes
- [ ] `/mode plan` — switches mode, system prompt changes
- [ ] `/mode explore` — switches, verify read-only behavior emphasis
- [ ] `/mode default` — returns to normal

### 10. Slash Commands

- [ ] `/goals` — shows goal tree (empty is fine)
- [ ] `/tasks list` — shows task queue
- [ ] `/agents list` — shows running agents
- [ ] `/memory stats` — shows memory statistics
- [ ] `/memory list` — lists memories (empty is fine on fresh install)

### 11. Streaming vs Non-Streaming

- [ ] Default run — text streams in real-time (visually verify)
- [ ] `./scaffold --no-stream "Tell me a joke"` — response appears all at once

### 12. Debug Output

- [ ] `./scaffold --debug "Hello"` — HTTP request/response traffic visible
- [ ] Verify API key is NOT shown in debug output

### 13. Memory System

- [ ] Ask scaffold to remember something: "Remember that my favorite color is blue"
- [ ] Exit and restart scaffold
- [ ] Ask "What is my favorite color?" — should recall from memory
- [ ] `/memory search blue` — should find the memory
- [ ] `/memory list` — memory entry visible

### 14. Version & Help

- [ ] `./scaffold --version` — prints version string with git hash
- [ ] `./scaffold --help` — prints usage info, exits cleanly

---

## P2 - Advanced Features

### 15. Subagent Spawning

- [ ] Ask scaffold to do something complex enough to trigger subagent ("Analyze this project's architecture and write a summary")
- [ ] Approval gate fires for subagent — approve
- [ ] Subagent runs, result is incorporated into response
- [ ] `/agents list` during execution — shows running subagent

### 16. GOAP Orchestration

- [ ] Give a multi-step task ("Create a Python script that fetches weather data, write tests for it, and review the code")
- [ ] Verify goals and actions are created (`/goals`)
- [ ] Observe supervisor/worker lifecycle
- [ ] `/tasks list` — shows work items

### 17. Todo Tool

- [ ] Ask scaffold to create a todo list for a task
- [ ] Verify TodoWrite and TodoRead tools are called
- [ ] Ask to check off items

### 18. Python Tool

- [ ] Ask scaffold to run Python code ("Use Python to calculate the first 10 Fibonacci numbers")
- [ ] Verify Python interpreter executes correctly
- [ ] Verify output is returned to the conversation

### 19. Web Fetch

- [ ] Ask scaffold to fetch a URL ("Fetch https://httpbin.org/get and summarize it")
- [ ] Approval gate fires for network category — approve
- [ ] Verify content is retrieved and summarized

### 20. Protected Files

- [ ] Ask scaffold to read `.env` — should be blocked (hard-blocked, no prompt)
- [ ] Ask scaffold to read `scaffold.config.json` — should be blocked
- [ ] Verify error message is clear

### 21. Image Attachment

- [ ] Create a simple PNG/JPEG test image
- [ ] Send `@/path/to/image.png describe this image` — verify image is sent to LLM
- [ ] Verify response describes the image (requires vision-capable model)

### 22. Configuration Cascade

- [ ] Set a custom model in `~/.local/scaffold/config.json`
- [ ] Set a different model in local `scaffold.config.json`
- [ ] Verify local config wins over user config
- [ ] Pass `--model` on CLI — verify CLI wins over both

### 23. MCP Server Integration

- [ ] Configure a simple MCP server in `scaffold.config.json` (e.g., a filesystem MCP server)
- [ ] Start scaffold — verify MCP server starts, tools are registered
- [ ] Use an MCP tool — approval gate fires, tool executes
- [ ] Remove MCP config — verify graceful degradation (no crash)

### 24. Custom Python Tools

- [ ] Create a custom `.py` tool in `~/.local/scaffold/tools/`
- [ ] Restart scaffold — tool should be discovered and registered
- [ ] Ask scaffold to use the custom tool — verify execution

### 25. JSON Output Mode

- [ ] `./scaffold --json "What is 2+2?"` — output is valid JSON
- [ ] Parse the JSON — verify structure contains response content

---

## P3 - Edge Cases & Robustness

### 26. Large Context Handling

- [ ] Feed a very large file (>1000 lines) and ask questions about it
- [ ] Verify conversation compaction works on long sessions (many turns)

### 27. Network Resilience

- [ ] Temporarily break the API URL in config — verify clear error message
- [ ] Verify retry logic works (briefly disconnect network during request)

### 28. Concurrent Operations

- [ ] Start multiple scaffold instances in different directories
- [ ] Verify no SQLite lock conflicts or data corruption

### 29. Signal Handling

- [ ] `Ctrl+C` during tool execution — cancels cleanly
- [ ] `Ctrl+C` twice rapidly — exits without crash
- [ ] `Ctrl+D` on empty prompt — exits cleanly

### 30. Update System

- [ ] `./scaffold --check-update` — reports current version and whether update is available
- [ ] (Careful) `./scaffold --update` — downloads and replaces binary if update available

### 31. AGENTS.md Project Context

- [ ] Create an `AGENTS.md` in CWD with project-specific instructions
- [ ] Start scaffold — verify context is appended to system prompt (observable via `--debug`)
- [ ] Use `@filename` syntax in AGENTS.md — verify referenced file is included

### 32. Allowlist Patterns

- [ ] `./scaffold --allow "shell:ls*"` — `ls` commands auto-approved, other shell commands still gated
- [ ] `./scaffold --allow-category=file_write` — all file writes auto-approved
