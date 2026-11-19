# CLI Reference

## Usage

```bash
scaffold [OPTIONS] [MESSAGE]
```

If `MESSAGE` is provided, scaffold processes it in one-shot mode. If the turn creates a goal (no explicit `--phase`), scaffold waits for that goal's supervisor to finish before exiting, printing progress lines as the goal runs, so the process doesn't return control while work is still in flight. Without a message, scaffold enters interactive mode.

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

### Authentication

| Flag | Description |
|------|-------------|
| `--login` | Log in to OpenAI via OAuth (ChatGPT subscription) |
| `--logout` | Log out of OpenAI OAuth session |

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
| `--phase plan\|execute` | Set supervisor phase (default: auto-detect from goal status) |

## Examples

```bash
# Interactive mode
./scaffold

# One-shot with a description
./scaffold "create a CLI todo app in Python with SQLite storage"

# Pipe a spec file
cat SPEC.md | ./scaffold

# Use a specific model
./scaffold --model gpt-4o "explain this codebase"

# Debug mode to see HTTP traffic
./scaffold --debug "why is the auth endpoint failing?"

# Log in with ChatGPT subscription (no API key needed)
./scaffold --login

# Log out of OAuth session
./scaffold --logout
```

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success — including a one-shot run whose goal(s) completed |
| 1 | Error — including a one-shot run whose goal failed (see stderr for `Goal <name> failed: <reason>` and the supervisor log path) |
| 130 | One-shot run interrupted (Ctrl+C) while waiting for a goal; the goal keeps running in the background |

## Environment

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `HOME` | User home directory (used for `~/.local/scaffold`) |
