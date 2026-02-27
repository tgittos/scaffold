# Getting Started

## Installation

Scaffold is a single binary with no runtime dependencies. Download it and run it.

```bash
# Download the latest release
wget https://github.com/tgittos/scaffold/releases/latest/download/scaffold

# Make it executable
chmod +x scaffold

# Optionally move it to your PATH
sudo mv scaffold /usr/local/bin/
```

Scaffold runs on Linux, macOS, Windows, FreeBSD, and OpenBSD across x86_64 and ARM64 architectures. One binary, every platform.

## Configuration

Scaffold needs an LLM API key to function. Set one of these environment variables:

```bash
# OpenAI (default provider)
export OPENAI_API_KEY="sk-..."

# Or Anthropic
export ANTHROPIC_API_KEY="sk-ant-..."
```

That's it. Scaffold auto-detects your provider and configures itself.

### Using a ChatGPT subscription (no API key needed)

If you have a ChatGPT subscription, log in via OAuth:

```bash
scaffold --login
```

This opens your browser for authentication. Once logged in, scaffold uses your subscription to access models through the Codex API. To log out:

```bash
scaffold --logout
```

For local models (LM Studio, Ollama, etc.), create a config file instead -- see [Configuration](configuration.md).

## Quick Start

### Interactive mode

Launch scaffold without arguments to enter an interactive session:

```bash
./scaffold
```

You'll get a prompt where you can describe what you want to build. Scaffold will ask clarifying questions, plan the work, and start building.

### One-shot mode

Pass your description as an argument for fully autonomous execution:

```bash
./scaffold "build a REST API with Express and SQLite for a todo app"
```

### Piping a spec

For larger projects, write a spec file and pipe it in:

```bash
cat PROJECT_SPEC.md | ./scaffold
```

## What happens next

When you give scaffold a description, it doesn't start writing code immediately. Here's the process:

1. **Planning** -- The orchestrator reads your description and decomposes it into a sequence of goals
2. **Goal creation** -- Each goal gets a supervisor agent with explicit completion criteria (boolean assertions about what should exist)
3. **Worker dispatch** -- Supervisors dispatch specialized workers (implementation, testing, code review, etc.) to do the actual building
4. **Verification** -- Each goal is verified complete before the orchestrator advances to the next one
5. **Completion** -- Repeat until the application exists

## Approval gates

By default, scaffold asks for your permission before writing files, running shell commands, or making network requests. You'll see prompts like:

```
[GATE] shell: npm install express
Allow? [y]es / [n]o / [a]lways allow this pattern
```

Your options:
- **y** -- Allow this one action
- **n** -- Deny this action
- **a** -- Allow this action and all future actions matching this pattern (session only)

To skip all prompts and let scaffold run fully autonomously:

```bash
./scaffold --yolo "build a blog with Next.js"
```

See [Configuration](configuration.md#approval-gates) for fine-grained control.

## Watching progress

While scaffold is building, use slash commands to inspect what's happening:

| Command | What it shows |
|---------|---------------|
| `/goals` | Goal tree with completion progress |
| `/tasks` | Task queue and worker status |
| `/agents` | Active supervisors and workers |
| `/memory` | Long-term semantic memory |
| `/mode` | Current behavioral mode |
| `/model` | Active LLM provider and model |

See [Interactive Mode](interactive-mode.md) for full details.

## Updating

Scaffold can update itself:

```bash
# Check if an update is available
./scaffold --check-update

# Download and apply the latest version
./scaffold --update
```

In interactive mode, scaffold checks for updates on startup and notifies you if a new version is available. Disable this with `"check_updates": false` in your config file.

## Next steps

- [Configuration](configuration.md) -- API keys, models, approval gates, and more
- [Interactive Mode](interactive-mode.md) -- Slash commands and real-time monitoring
- [Autonomous Mode](autonomous-mode.md) -- How the orchestrator, supervisors, and workers operate
- [Memory System](memory.md) -- Long-term semantic memory
- [Custom Tools](custom-tools.md) -- Add your own Python tools and worker role prompts
- [MCP Servers](mcp.md) -- Connect external tool servers
- [CLI Reference](cli-reference.md) -- Complete command-line flag reference
