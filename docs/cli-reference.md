# CLI Reference

## Usage

```bash
scaffold [OPTIONS] [MESSAGE]
```

If `MESSAGE` is provided, scaffold processes it and exits (one-shot mode). Without a message, scaffold enters interactive mode.

## Options

### General

| Flag | Description |
|------|-------------|
| `-h`, `--help` | Show help message and exit |
| `-v`, `--version` | Show version information and exit |
| `--debug` | Enable debug output (shows HTTP requests to LLM providers) |
| `--no-stream` | Disable response streaming |
| `--json` | Enable JSON output mode for programmatic integration |
| `--home <path>` | Override home directory (default: `~/.local/scaffold`) |

### Model and prompt

| Flag | Description |
|------|-------------|
| `--model <name>` | Override the default model for this session |
| `--system-prompt-file <path>` | Load a custom system prompt from file |

### Approval gates

| Flag | Description |
|------|-------------|
| `--yolo` | Disable all approval gates for this session |
| `--allow <spec>` | Add an allowlist entry (see below) |
| `--allow-category=<cat>` | Allow an entire category (`file_read`, `file_write`, `shell`, `network`, `memory`, `subagent`, `mcp`, `python`) |

#### `--allow` format

Regex pattern for file/network tools:
```bash
./scaffold --allow "write_file:^src/.*\\.js$"
```

Shell command prefix:
```bash
./scaffold --allow "shell:npm,install"
```

### Updates

| Flag | Description |
|------|-------------|
| `--check-update` | Check for updates and exit |
| `--update` | Download and apply the latest update |

### Messaging

| Flag | Description |
|------|-------------|
| `--no-auto-messages` | Disable automatic message polling |
| `--message-poll-interval <ms>` | Set message poll interval (minimum 100ms) |

### Internal flags

These are used by scaffold's agent system and are not typically invoked directly:

| Flag | Description |
|------|-------------|
| `--subagent` | Run in subagent mode (requires `--task`) |
| `--task <desc>` | Task description for subagent |
| `--context <ctx>` | Additional context for subagent |
| `--worker` | Run in worker mode (requires `--queue`) |
| `--queue <name>` | Worker queue name |
| `--supervisor` | Run in supervisor mode (requires `--goal`) |
| `--goal <id>` | Goal ID for supervisor |

## Examples

```bash
# Interactive mode
./scaffold

# One-shot with a description
./scaffold "create a CLI todo app in Python with SQLite storage"

# Pipe a spec file
cat SPEC.md | ./scaffold

# Fully autonomous (no approval prompts)
./scaffold --yolo "build a REST API with Express"

# Use a specific model
./scaffold --model gpt-4o "explain this codebase"

# Debug mode to see HTTP traffic
./scaffold --debug "why is the auth endpoint failing?"

# Allow all shell commands for this session
./scaffold --allow-category=shell

# Allow specific file writes
./scaffold --allow "write_file:^src/.*" "add logging to the server"
```

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error |

## Environment

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `HOME` | User home directory (used for `~/.local/scaffold`) |
