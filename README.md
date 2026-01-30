# Ralph

**A Portable AI Development Assistant**

Ralph is an AI-powered command-line tool that helps developers write code, debug issues, and automate workflows. Built as a single portable binary using [Cosmopolitan Libc](https://github.com/jart/cosmopolitan), Ralph runs on Linux, macOS, Windows, FreeBSD, NetBSD, and OpenBSD without installation or dependencies.

## Why Ralph?

Most AI coding tools require specific environments, package managers, or runtimes. Ralph takes a different approach: a single binary that runs anywhere and grows with your project.

**Bring AI anywhere.** Copy one file to a server, CI runner, or colleague's machine and it works. No installation, no dependencies, no containers. Useful for air-gapped systems, quick SSH sessions, or environments where you can't (or don't want to) install software.

**Fits into your workflow.** Ralph follows Unix conventions—it reads from stdin, accepts pipes, and runs one-shot commands. Build AI into existing scripts: `git diff | ralph "summarize"` or `make 2>&1 | ralph "explain these errors"`. It's a tool in your toolchain, not a replacement for it.

**Accumulates project knowledge.** The memory system, AGENTS.md support, and extensible tools combine so Ralph becomes more useful over time. Store coding conventions, past decisions, and domain knowledge. New team members can query what the AI has learned about your codebase.

**Extends itself.** Ralph can write Python tools for tasks you do repeatedly. Ask it to build a tool for your specific workflow, and that tool is available in future sessions. The assistant adapts to your project's needs.

**Works offline.** Point Ralph at a local model (LM Studio, Ollama) and you have private, offline AI assistance. Same binary, same interface, no data leaves your machine.

## What Ralph Does

Ralph connects to LLM providers (OpenAI, Anthropic, or local models) and gives the AI access to tools for:

- **File operations**: Read, write, and modify files in your project
- **Shell commands**: Execute build commands, run tests, manage git
- **Semantic memory**: Store and recall information across sessions using vector embeddings
- **PDF processing**: Extract text from PDF documentation for context
- **Task tracking**: Maintain a todo list that persists between sessions
- **Python execution**: Run Python scripts with output capture
- **MCP integration**: Connect to external MCP servers you configure

## Interactive Mode

Ralph's interactive mode provides a conversational interface for working through problems. Context persists throughout the session, and Ralph can work on multiple things concurrently:

```bash
$ ./ralph
> My test suite has been flaky lately. Sometimes test_parse_response fails.

[Ralph spawns agents to investigate: one checks for timing dependencies,
another looks for shared mutable state, a third examines test isolation.
Results stream back as they're found.]

> While you're looking at that, what's the complexity of our cache eviction?

[You can discuss other topics while background investigation continues.
When an agent finds the race condition, it interrupts with the result.]

> Good find. Fix it, but keep the tests running in the background so I
> know immediately if something else breaks.

[Ralph fixes the issue and sets up a background watcher on the test output.]
```

## One-Shot Mode

For automation and scripting, Ralph accepts a message as an argument:

```bash
# Generate a commit message
git diff --staged | ./ralph "Write a commit message for these changes"

# Analyze test failures
pytest 2>&1 | ./ralph "What's causing these test failures?"

# Quick code review
./ralph "Review this function for bugs" < myfile.c
```

## Installation

### Download

```bash
wget https://github.com/bluetongueai/ralph/releases/latest/download/ralph
chmod +x ralph
```

### Build from Source

```bash
git clone https://github.com/bluetongueai/ralph
cd ralph
make
```

## Configuration

Ralph uses a JSON configuration file (`ralph.config.json`):

```json
{
    "api_url": "https://api.openai.com/v1/chat/completions",
    "model": "gpt-4o",
    "openai_api_key": "sk-your-key-here",
    "context_window": 128000,
    "max_tokens": -1
}
```

### Configuration Options

| Field | Description | Default |
|-------|-------------|---------|
| `api_url` | LLM API endpoint | `https://api.openai.com/v1/chat/completions` |
| `model` | Model identifier | `gpt-4o-mini` |
| `context_window` | Model context window size | `8192` |
| `max_tokens` | Max response tokens (-1 for auto) | `-1` |
| `openai_api_key` | OpenAI API key | None |
| `anthropic_api_key` | Anthropic API key | None |
| `embedding_api_url` | Embeddings endpoint | Uses OpenAI |
| `embedding_model` | Embedding model | `text-embedding-3-small` |

### Provider Examples

**OpenAI:**
```json
{
    "api_url": "https://api.openai.com/v1/chat/completions",
    "model": "gpt-4o",
    "openai_api_key": "sk-your-key",
    "context_window": 128000
}
```

**Anthropic:**
```json
{
    "api_url": "https://api.anthropic.com/v1/messages",
    "model": "claude-sonnet-4-20250514",
    "anthropic_api_key": "sk-ant-your-key",
    "openai_api_key": "sk-your-key",
    "context_window": 200000
}
```

**Local (LM Studio/Ollama):**
```json
{
    "api_url": "http://localhost:1234/v1/chat/completions",
    "model": "qwen/qwen-2.5-coder-32b",
    "context_window": 32768
}
```

### Environment Variables

Ralph auto-detects these environment variables:

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key (required for embeddings) |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `EMBEDDING_MODEL` | Override embedding model |
| `OPENAI_API_URL` | Override OpenAI endpoint |

On first run, Ralph creates a config file using any API keys found in your environment.

## Features

### Memory System

Ralph includes a semantic memory system backed by a vector database. The AI can store and recall information across sessions:

```bash
> remember that our API uses snake_case for all field names

[Stored in memory]

# Later session...
> What naming convention do we use for API fields?

[Recalls the stored preference]
```

Memory persists in `~/.local/ralph/` and uses OpenAI embeddings for semantic search.

### PDF Processing

Ralph can extract text from PDF files and use the content as context:

```bash
./ralph "What authentication methods does this API support?" --pdf api-docs.pdf
```

### MCP Integration

Ralph supports the [Model Context Protocol](https://modelcontextprotocol.io/) for connecting to external tool servers. Configure servers in `ralph.config.json`:

```json
{
    "mcpServers": {
        "my-server": {
            "type": "stdio",
            "command": "/path/to/server",
            "args": ["--flag"]
        }
    }
}
```

Ralph discovers and registers tools from configured servers at startup.

### Parallel Agents

Ralph can spawn subagents that work concurrently, communicate results back, and interrupt the main conversation when they find something relevant. This isn't just parallelism for speed—it changes how you can interact with the assistant.

**Speculative exploration**: "Why is this slow?" Ralph spawns agents to profile the hot path, check for N+1 queries, and examine memory allocation patterns. The first conclusive finding interrupts with an answer; the others continue and may surface additional issues.

**Background monitoring**: Start a long build or test run, then continue discussing other code. When something fails, Ralph interrupts with context about what went wrong.

**Progressive refinement**: Get a quick approximation immediately, then receive refined analysis as deeper investigation completes. Useful for large codebases where thorough searches take time.

**Tool composition**: Custom Python tools can participate in the messaging system. A file watcher tool notifies an analysis agent, which notifies you—reactive pipelines without polling.

Configuration: `max_subagents` (default: 5), `subagent_timeout` (default: 300s). The messaging layer persists across sessions.

### Extensible Python Tools

Ralph's capabilities come from Python tools in `~/.local/ralph/tools/`. The AI can write new tools or modify existing ones—ask for a tool, and it becomes part of Ralph's permanent repertoire.

```bash
> I need a tool that finds all SQL queries in the codebase and checks
> them for injection vulnerabilities

[Ralph writes sql_audit.py to ~/.local/ralph/tools/]

# Next session, Ralph can use it autonomously when relevant
> Review the user registration flow for security issues

[Ralph automatically uses sql_audit alongside other analysis]
```

Tools can also participate in the messaging system—a file watcher tool can notify agents when files change, enabling reactive workflows without polling. On startup, Ralph scans `~/.local/ralph/tools/` for `.py` files; each function becomes a tool using its name and docstring.

### AGENTS.md Support

Ralph reads `AGENTS.md` files following the [agents.md specification](https://agents.md/) to customize AI behavior for specific projects.

## Command Line Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `--debug` | Enable debug output (shows HTTP requests and data exchange) |
| `--no-stream` | Disable response streaming |
| `--json` | Enable JSON output mode for programmatic integration |
| `--home <path>` | Override Ralph home directory (default: `~/.local/ralph`) |
| `--yolo` | Disable all approval gates for the session |

## Usage Examples

### One-Shot Commands
```bash
# Generate a commit message from staged changes
git diff --staged | ./ralph "Write a commit message for these changes"

# Understand unfamiliar code
./ralph "Explain how this regex works: ^(?=.*[A-Z])(?=.*[0-9]).{8,}$"

# Quick code review
git diff HEAD~1 | ./ralph "Review these changes for potential issues"
```

### Investigative Workflows
```bash
# Debug a problem with parallel investigation
./ralph "Production is returning 502s intermittently. Check the nginx config,
         the upstream health checks, and recent deployments."

# Understand a new codebase
./ralph "I just cloned this repo. Give me an overview of the architecture,
         then dig into how authentication works."
```

### Build a Custom Tool, Then Use It
```bash
> I frequently need to check which functions call a given function.
> Build me a tool for that.

[Ralph writes a call_graph.py tool to ~/.local/ralph/tools/]

# Next session, the tool is available
> What calls parse_response?

[Ralph uses the call_graph tool it created]
```

### Background Monitoring
```bash
> Run the full test suite in the background. While that's running,
> let's refactor the cache module.

[Tests run in background; Ralph interrupts if something fails]

> Good, tests passed. Now deploy to staging and let me know when
> the health check passes.

[Ralph monitors staging while you context-switch to other work]
```

## Data Storage

Ralph stores data in `~/.local/ralph/`:
- `CONVERSATION.md` - Conversation history
- `ralph.db` - SQLite database (tasks, messages)
- `*.idx` / `*.meta` - Vector database indices (HNSWLIB format)
- `documents/` - Document storage (JSON)
- `metadata/` - Chunk metadata (JSON)
- `tools/` - Custom Python tools

## Platform Support

Ralph runs on:
- Linux (x86_64, aarch64)
- macOS (x86_64, Apple Silicon)
- Windows
- FreeBSD, NetBSD, OpenBSD

No runtime dependencies required.

## Building

Requirements:
- Cosmopolitan toolchain (included in devcontainer)
- Make

```bash
make        # Build ralph
make test   # Run tests
make clean  # Clean build artifacts
```

## License

See LICENSE file.

---

Built with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan).
