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

Ralph's interactive mode provides a conversational interface for working through problems iteratively:

```bash
$ ./ralph
Ralph Interactive Mode
Type 'quit' or 'exit' to end the conversation.

> I'm getting a segfault in my linked list implementation

[Ralph examines your code, identifies the issue, explains it, and offers a fix]

> Can you also add a test case that would have caught this?

[Ralph creates a test file with the failing case]
```

The conversation context persists throughout the session, so Ralph remembers what you've discussed and can build on previous work.

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

### Subagents

Ralph can spawn parallel subagents for complex tasks. This is useful when the AI needs to work on multiple independent subtasks simultaneously.

Configuration:
- `max_subagents`: Maximum concurrent subagents (default: 5)
- `subagent_timeout`: Timeout in seconds (default: 300)

### Extensible Python Tools

Ralph's file and shell operations are implemented as Python tools stored in `~/.local/ralph/tools/`. The AI can write new tools or modify existing ones:

```bash
> I need a tool that converts CSV files to JSON

[Ralph writes a new csv_to_json.py file to ~/.local/ralph/tools/]

# After restarting Ralph, the new tool is available
> Convert data.csv to JSON format

[Ralph uses the csv_to_json tool it created]
```

On startup, Ralph scans `~/.local/ralph/tools/` for `.py` files. Each Python function becomes a registered tool using its function name and docstring. Default tools include `read_file`, `write_file`, `shell`, `search_files`, `web_fetch`, and others.

### AGENTS.md Support

Ralph reads `AGENTS.md` files following the [agents.md specification](https://agents.md/) to customize AI behavior for specific projects.

## Usage Examples

### Code Review
```bash
git diff HEAD~1 | ./ralph "Review these changes for potential issues"
```

### Debug Assistance
```bash
./ralph "I'm getting 'undefined reference' errors when linking. Here's my Makefile..."
```

### Learning
```bash
./ralph "Explain how this regex works: ^(?=.*[A-Z])(?=.*[0-9]).{8,}$"
```

### Refactoring
```bash
./ralph "This function is too long. Suggest how to break it into smaller functions" < big_function.c
```

## Data Storage

Ralph stores data in `~/.local/ralph/`:
- `CONVERSATION.md` - Conversation history
- Vector database files (HNSWLIB format)
- Memory metadata (JSON)
- Python tools

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
