# Ralph

An AI development assistant built as a single portable C binary. Runs on Linux, macOS, Windows, FreeBSD, NetBSD, and OpenBSD. No dependencies. No installation.

Built with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan).

## Why Ralph?

Most AI coding assistants require specific runtimes, package managers, or cloud services. Ralph is a file you can copy anywhere.

**Portable.** One binary runs everywhere. Drop it on a server, CI runner, or air-gapped system and it works. No Python. No Node. No containers.

**Unix-native.** Ralph reads stdin, writes stdout, and composes with pipes. Build AI into existing workflows: `git diff | ralph "summarize"` or `make 2>&1 | ralph "explain these errors"`. It's a tool in your toolchain, not a replacement for it.

**Accumulates knowledge.** Coding conventions, architectural decisions, and domain knowledge are stored in a vector database and recalled automatically. Ralph becomes more useful the longer you use it.

**Extends itself.** Ralph ships with an embedded Python interpreter. Ask it to build a tool for your workflow, and that tool persists across sessions. The assistant adapts to your project.

**Works offline.** Point Ralph at a local model (LM Studio, Ollama) and you have private, air-gapped AI assistance. Same binary, same interface, no data leaves your machine.

## What It Does

Ralph connects to LLM providers (OpenAI, Anthropic, or local models) and gives the AI access to 36+ tools:

- **File operations** -- Read, write, search, and apply diffs
- **Shell commands** -- Execute builds, run tests, manage git -- with approval gates
- **Semantic memory** -- Store and recall information across sessions via vector embeddings
- **Knowledge bases** -- Build searchable indices from text, PDFs, or raw embeddings
- **Task tracking** -- Persistent, hierarchical todo list with dependencies
- **Parallel agents** -- Spawn subagents that work concurrently and report back
- **Python execution** -- Run scripts and build custom tools that persist
- **MCP integration** -- Connect to Model Context Protocol servers
- **Web fetching** -- Retrieve and process content from URLs
- **PDF processing** -- Extract, chunk, and index PDF documents

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

## Quick Start

Ralph needs an LLM API key. Set environment variables or create `ralph.config.json`:

```json
{
    "api_url": "https://api.openai.com/v1/chat/completions",
    "model": "gpt-4o",
    "openai_api_key": "sk-your-key-here"
}
```

Ralph auto-detects the provider from the API URL and adjusts its request format accordingly.

### Interactive Mode

```bash
$ ./ralph
> My test suite is flaky. Sometimes test_parse_response fails.

Let me take a look at that test. [Ralph reads the test file, examines
the implementation, and runs it a few times to see the failure.]

The test has a race condition in the response parser -- it assumes
messages arrive in order but the handler processes them asynchronously.
Want me to fix it?

> Yes, and while that's running, what's the complexity of our cache eviction?

[Ralph applies the fix and runs tests in the background while answering
your cache question. When the test run finishes, it reports back.]

All tests passing. The fix added proper sequencing to the response handler.
```

When you return for a new session, Ralph loads recent conversation history from its vector database, generates a recap of where you left off, and automatically recalls any stored memories relevant to what you're working on.

### One-Shot Mode

For automation and scripting, pass a message as an argument:

```bash
# Generate a commit message
git diff --staged | ./ralph "Write a commit message for these changes"

# Analyze test failures
pytest 2>&1 | ./ralph "What's causing these test failures?"

# Code review
./ralph "Review this function for bugs" < myfile.c
```

One-shot mode has full access to Ralph's memory and knowledge bases -- past context informs every response.

## Configuration

Configuration cascades: local `ralph.config.json` > `~/.local/ralph/config.json` > defaults. Environment variables for API keys (`OPENAI_API_KEY`, `ANTHROPIC_API_KEY`) override config file values when set.

On first run, Ralph creates a config file using any API keys found in your environment.

### Options

| Field | Description | Default |
|-------|-------------|---------|
| `api_url` | LLM API endpoint | `https://api.openai.com/v1/chat/completions` |
| `model` | Model identifier | `gpt-5-mini-2025-08-07` |
| `context_window` | Context window size | Auto-detected from model |
| `max_tokens` | Max response tokens (-1 for auto) | `-1` |
| `openai_api_key` | OpenAI API key | None |
| `anthropic_api_key` | Anthropic API key | None |
| `embedding_api_url` | Embeddings endpoint | Uses main API URL |
| `embedding_model` | Embedding model | `text-embedding-3-small` |
| `system_prompt` | Custom system prompt | None |
| `enable_streaming` | Stream responses | `true` |
| `max_subagents` | Maximum concurrent subagents | `5` |
| `subagent_timeout` | Subagent timeout in seconds | `300` |
| `api_max_retries` | Max retries on failure | `3` |
| `api_retry_delay_ms` | Initial retry delay | `1000` |
| `api_backoff_factor` | Retry backoff multiplier | `2.0` |

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

Note: OpenAI key is still needed for embeddings when using Anthropic as the LLM provider.

**Local (LM Studio/Ollama):**
```json
{
    "api_url": "http://localhost:1234/v1/chat/completions",
    "model": "qwen/qwen-2.5-coder-32b",
    "context_window": 32768
}
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key (overrides config; required for embeddings) |
| `ANTHROPIC_API_KEY` | Anthropic API key (overrides config) |
| `RALPH_HOME` | Override Ralph home directory (default: `~/.local/ralph`) |
| `EMBEDDING_API_URL` | Override embedding API endpoint |
| `EMBEDDING_MODEL` | Override embedding model |
| `OPENAI_API_URL` | Override OpenAI endpoint |

## Features

### Semantic Memory

Ralph's memory system is backed by HNSWLIB and OpenAI embeddings. Tell Ralph to remember something and it embeds the text into a persistent semantic index. In future sessions, relevant memories are automatically retrieved and injected into the system prompt -- no explicit recall needed.

```bash
# Session 1: Teach Ralph about your project
> remember that our API uses snake_case for all field names
> remember that we use PostgreSQL 15 and all migrations go in db/migrate/
> remember that the billing service is maintained by the payments team
>   and changes need their review

# Session 2: Memories surface automatically when relevant
> Write a new endpoint for user preferences

[Ralph retrieves the snake_case convention from memory and uses it in the
generated code. The API style is consistent without being reminded.]
```

Manage memories directly with slash commands:

| Command | Description |
|---------|-------------|
| `/memory list` | List all stored memories |
| `/memory search <query>` | Search memories by content |
| `/memory show <id>` | Show full details of a memory |
| `/memory edit <id> <field> <value>` | Edit a memory (re-embeds if content changes) |
| `/memory indices` | List all vector indices and their stats |
| `/memory stats` | Show index statistics |

### Knowledge Bases

Ralph exposes 13 vector database tools for building structured, searchable knowledge bases. Ingest PDFs, documents, or raw text into named indices and query them semantically.

```bash
# Build a knowledge base from documentation
> Index our API docs and the architecture RFC into a "project_docs" knowledge base

[Ralph creates a vector index, chunks the documents with overlap, embeds
each chunk, and stores them. A 40-page PDF becomes ~187 searchable chunks.]

# Query naturally
> How does our rate limiting work? Check the project docs.

[Ralph searches the index, retrieves relevant chunks, and answers based
on the documentation.]
```

The AI can create purpose-specific indices (`research_papers`, `codebase_docs`, `meeting_notes`), each with independent configuration. Documents persist across sessions in `~/.local/ralph/`.

### Parallel Agents

Ralph spawns subagents as separate processes. Each subagent is a full Ralph instance with access to all tools. They communicate results back through a SQLite-backed messaging system, and the parent's async executor lets you keep chatting while background work runs.

```bash
> The dashboard page is slow. Spawn three agents to check database queries,
> API response times, and frontend bundle size in parallel.

[Ralph spawns three subagents. You can continue the conversation while they
work. Results arrive as each subagent finishes.]

Agent 1: Database queries look normal, no N+1 issues found.
Agent 2: API response times averaged 45ms, within expected range.
Agent 3: Frontend bundle is 2.3MB uncompressed -- largest chunk is the
charting library at 890KB.
```

**How it works:**

- Parent forks child processes with isolated conversation contexts
- Communication flows through SQLite: completion messages, direct messages, and pub/sub channels
- The parent's `select()` loop monitors stdin, notification pipes, and approval channels simultaneously
- Subagent approval requests proxy up to the parent for TTY prompting -- you see exactly what every agent wants to do

Configuration: `max_subagents` (default: 5), `subagent_timeout` (default: 300s).

### Extensible Python Tools

Ralph ships with an embedded CPython 3.12 interpreter and built-in tools for file operations, shell execution, web fetching, and directory listing. On first run, these are extracted to `~/.local/ralph/tools/`. The AI can write new tools or modify existing ones -- ask for a tool, and it becomes part of Ralph's permanent repertoire.

```bash
> I need a tool that analyzes our test coverage and highlights untested code paths

[Ralph writes coverage_analyzer.py to ~/.local/ralph/tools/. The tool uses
docstring conventions so Ralph understands its parameters, gate category,
and approval requirements.]

# Next session, Ralph can use it autonomously
> Are there any gaps in our auth module's test coverage?

[Ralph automatically invokes the coverage_analyzer tool it created in the
previous session, alongside its built-in file search tools.]
```

**Tool discovery**: On startup, Ralph scans `~/.local/ralph/tools/` for `.py` files. Each function becomes a tool using its name, docstring, and type annotations. Gate categories (like `file_read` or `shell`) are declared via `Gate:` directives in the docstring, integrating custom tools with the approval system.

**Persistent interpreter**: The Python interpreter stays alive for the entire session. Tools can share state -- a data-loading tool can cache results that a subsequent analysis tool reuses.

**Built-in tools** (in `~/.local/ralph/tools/`):

| Tool | Description | Gate |
|------|-------------|------|
| `read_file` | Read file contents with line ranges | file_read |
| `write_file` | Write content to file | file_write |
| `append_file` | Append content to file | file_write |
| `apply_delta` | Apply unified diffs | file_write |
| `list_dir` | List directory contents | file_read |
| `search_files` | Search files by content or pattern | file_read |
| `shell` | Execute shell commands with timeout | shell |
| `web_fetch` | Fetch and process web content | network |
| `file_info` | Get file metadata | file_read |

### Approval Gates

Ralph controls which tools can execute without asking. Every tool belongs to a category, and each category has an action: `allow` (run freely), `gate` (ask first), or `deny` (block entirely).

| Category | Default | Tools |
|----------|---------|-------|
| `file_read` | allow | read_file, list_dir, search_files, file_info |
| `file_write` | gate | write_file, append_file, apply_delta |
| `shell` | gate | shell command execution |
| `network` | gate | web_fetch |
| `memory` | allow | remember, recall, todo, vector_db operations |
| `subagent` | gate | spawning subagents |
| `mcp` | gate | MCP tool execution |
| `python` | allow | Python interpreter |

When prompted, you can:
- **y**: Allow this operation
- **n**: Deny (triggers exponential backoff after repeated denials)
- **a**: Allow always -- Ralph generates a pattern (regex for paths/URLs, prefix match for shell commands) and adds it to the session allowlist
- **?**: Show full details of the operation

**Trust builds over time**: "Allow always" patterns can be saved to `ralph.config.json` so they persist across sessions. Subagents inherit only config-file patterns, not session-added ones, preventing privilege escalation.

**Protected files**: `ralph.config.json` and `.env*` files are hard-blocked from modification, even with `--yolo`. The system tracks inodes to catch renames and hardlinks.

```json
{
    "approval_gates": {
        "enabled": true,
        "categories": {
            "file_write": "gate",
            "shell": "gate",
            "network": "allow"
        },
        "allowlist": [
            {"tool": "write_file", "pattern": "^src/.*\\.py$"},
            {"tool": "shell", "command": ["git", "status"]}
        ]
    }
}
```

### Task Tracking

Ralph maintains a persistent todo list backed by SQLite. Tasks survive across sessions, support hierarchies and dependencies, and are automatically injected into the system prompt so the AI always knows what's pending.

```bash
# Session 1
> Break down the auth refactor into tasks

[Ralph creates a task hierarchy:
 1. Extract session middleware (high)
   1.1 Write session store interface
   1.2 Implement Redis backend
   1.3 Add session tests
 2. Migrate endpoints (medium)
 3. Remove old auth code (low, blocked by 1 and 2)]

# Session 2 (days later)
> What was I working on?

[Ralph loads the task list, sees items 1.1 and 1.2 are complete, 1.3 is
in progress. Recalls relevant memories about your session store design.
Suggests picking up where you left off on the session tests.]
```

### MCP Integration

Ralph supports the [Model Context Protocol](https://modelcontextprotocol.io/) for connecting to external tool servers. At startup, Ralph connects to each configured server, discovers available tools via JSON-RPC, and registers them with a `mcp_<server>_<tool>` naming convention.

```json
{
    "mcpServers": {
        "github": {
            "command": "npx",
            "args": ["-y", "@modelcontextprotocol/server-github"],
            "env": {"GITHUB_TOKEN": "${GITHUB_TOKEN}"}
        },
        "postgres": {
            "command": "npx",
            "args": ["-y", "@modelcontextprotocol/server-postgres"],
            "env": {"DATABASE_URL": "${DATABASE_URL}"}
        }
    }
}
```

**Transport support**: STDIO (local subprocess), HTTP, and SSE (Server-Sent Events). Environment variable expansion supports `${VAR}` and `${VAR:-default}` syntax.

### AGENTS.md

Ralph reads `AGENTS.md` in the current directory to customize AI behavior per project. The file supports `@FILENAME` references to include other files inline -- reference your architecture docs, coding standards, or API specifications directly.

```markdown
# Project Instructions

Always use our error handling pattern from @ERROR_HANDLING.md.
See @ARCHITECTURE.md for service boundaries.
Never modify files in vendor/ without approval.
```

Referenced files are wrapped in XML tags and included in the system prompt, giving Ralph project-specific context without manual copy-pasting.

### Conversation Management

Ralph automatically manages long conversations. A token manager estimates context usage, and when the conversation approaches the context window limit, a compactor generates a rolling summary of older messages and trims the history. The summary is injected into subsequent prompts, so context is preserved even as individual messages are dropped.

Conversation messages are stored in the vector database, so relevant exchanges from past sessions can be retrieved semantically even after compaction.

## Command Line Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `--debug` | Enable debug output (shows HTTP traffic) |
| `--no-stream` | Disable response streaming |
| `--json` | JSON output mode for programmatic integration |
| `--home <path>` | Override Ralph home directory (default: `~/.local/ralph`) |
| `--yolo` | Disable all approval gates (protected files still blocked) |
| `--allow <spec>` | Add to session allowlist (e.g., `shell:git,status`) |
| `--allow-category=<cat>` | Set category to allow (e.g., `--allow-category=file_write`) |

## Usage Examples

### Pipe-Driven Automation

```bash
# Commit message from staged changes
git diff --staged | ./ralph "Write a commit message for these changes"

# Explain build errors
make 2>&1 | ./ralph "What's wrong and how do I fix it?"

# Code review in CI
git diff origin/main...HEAD | ./ralph --json "Review for bugs and security issues"
```

### Building Project Knowledge

```bash
# Session 1: Onboard Ralph to your project
> remember that we use a hexagonal architecture -- ports in src/ports/,
>   adapters in src/adapters/, domain logic in src/domain/
> remember that all database access goes through repository interfaces,
>   never direct SQL in domain code
> Index the API specification into a "specs" knowledge base
>   @docs/openapi.pdf

# Session 2: Ralph applies what it learned
> Add a new endpoint for user notifications

[Ralph retrieves the hexagonal architecture memory and creates the port
interface first, then the adapter. It checks the "specs" knowledge base
and implements the endpoint to match the existing API conventions.]
```

### Custom Tools That Compound

```bash
# Session 1: Build a tool for your workflow
> I keep manually checking if our database migrations are reversible.
>   Build me a tool that analyzes a migration and reports whether the
>   down migration properly reverses the up migration.

[Ralph writes migration_checker.py with schema-aware analysis]

# Session 2: The tool integrates with other capabilities
> Review PR #42. It has migration files -- run the migration checker on them.

[Ralph reads the PR diff, runs the migration_checker tool on each migration
file, and reports the results.]

# Session 3: Extend the tool
> The migration checker should also verify that new indices have
>   concurrent creation enabled.

[Ralph reads the existing tool, adds the new check. The updated version
is immediately available.]
```

### Parallel Investigation

```bash
> Our API response times jumped 3x after yesterday's deploy. Spawn agents to
> diff yesterday's commits, profile the hot paths, and check database query
> plans. I need to prep the rollback script while you investigate.

[Ralph spawns three subagents. You work on the rollback script while they
investigate. Results arrive as they finish:]

Agent 1: Commit abc123 added an N+1 query in the order listing endpoint.
Agent 2: Profile shows 80% of time in database calls for that endpoint.
Agent 3: Query plan confirms sequential scans on shipping_addresses.

> Fix the N+1 query, run the tests, and remember that the orders endpoint
> needs the shipping_addresses join.

[Ralph fixes the query, runs tests in the background, and stores the note
in memory for future reference.]
```

### JSON Output for CI/CD

```bash
# Machine-readable output
./ralph --json "List all TODO comments in src/" | jq '.result'

# Structured code review in a pipeline
git diff origin/main...HEAD | ./ralph --json "Review for bugs" | jq '.result'
```

The `--json` flag outputs newline-delimited JSON objects with a `type` field (`assistant`, `system`, `result`, `tool_result`, `error`) for structured parsing.

### Research Assistant

```bash
# Ingest research materials
> Create a "papers" knowledge base. Index these PDFs:
>   @/research/transformer_attention.pdf
>   @/research/sparse_mixture_of_experts.pdf
>   @/research/flash_attention_v2.pdf

[Ralph extracts text, chunks with overlap, and builds the index.]

# Cross-reference across documents
> Compare how Flash Attention and the original Transformer paper describe
> the attention computation. What did Flash Attention change?

[Ralph searches the index, retrieves relevant chunks from both papers,
and provides a comparison based on the content.]

# Connect research to implementation
> Based on what the papers say, review our attention implementation in
> src/model/attention.py for potential optimizations

[Ralph searches the knowledge base, reads your implementation, and suggests
changes based on the indexed research.]
```

## Data Storage

Ralph stores persistent data in `~/.local/ralph/` (override with `--home` or `RALPH_HOME`):

| Path | Format | Purpose |
|------|--------|---------|
| `tasks.db` | SQLite | Task persistence |
| `messages.db` | SQLite | Inter-agent messaging |
| `*.index` / `*.index.meta` | HNSWLIB binary + JSON | Vector database indices |
| `documents/` | JSON files | Full document content per index |
| `metadata/` | JSON files | Chunk metadata per index |
| `tools/` | Python files | Custom and built-in Python tools |
| `config.json` | JSON | User configuration (fallback) |

All vector indices, messages, and tasks persist across sessions. The messaging system cleans up per-agent messages when agents exit, but channel history and stored documents remain.

## Platform Support

Ralph runs on:
- Linux (x86_64, aarch64)
- macOS (x86_64, Apple Silicon)
- Windows (core features; subagent system uses POSIX APIs)
- FreeBSD, NetBSD, OpenBSD

No runtime dependencies required.

## Building

Requirements:
- Docker (devcontainer with Cosmopolitan toolchain)
- Make

```bash
./scripts/build.sh          # Build ralph (compact output)
./scripts/build.sh -v       # Build with full output
./scripts/run_tests.sh      # Run test suite with segfault detection
./scripts/build.sh clean    # Clean build artifacts
```

## License

See LICENSE file.
