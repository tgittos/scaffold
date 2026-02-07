# Ralph

**A Portable AI Development Assistant**

Ralph is an AI-powered command-line tool that helps developers write code, debug issues, and automate workflows. Built as a single portable binary using [Cosmopolitan Libc](https://github.com/jart/cosmopolitan), Ralph runs on Linux, macOS, Windows, FreeBSD, NetBSD, and OpenBSD without installation or dependencies.

## Why Ralph?

Most AI coding tools require specific environments, package managers, or runtimes. Ralph takes a different approach: a single binary that runs anywhere and grows with your project.

**Bring AI anywhere.** Copy one file to a server, CI runner, or colleague's machine and it works. No installation, no dependencies, no containers. Useful for air-gapped systems, quick SSH sessions, or environments where you can't (or don't want to) install software.

**Fits into your workflow.** Ralph follows Unix conventions -- it reads from stdin, accepts pipes, and runs one-shot commands. Build AI into existing scripts: `git diff | ralph "summarize"` or `make 2>&1 | ralph "explain these errors"`. It's a tool in your toolchain, not a replacement for it.

**Accumulates project knowledge.** The memory system, AGENTS.md support, and extensible tools combine so Ralph becomes more useful over time. Store coding conventions, past decisions, and domain knowledge. New team members can query what the AI has learned about your codebase.

**Extends itself.** Ralph can write Python tools for tasks you do repeatedly. Ask it to build a tool for your specific workflow, and that tool is available in future sessions. The assistant adapts to your project's needs.

**Works offline.** Point Ralph at a local model (LM Studio, Ollama) and you have private, offline AI assistance. Same binary, same interface, no data leaves your machine.

## What Ralph Does

Ralph connects to LLM providers (OpenAI, Anthropic, or local models) and gives the AI access to tools for:

- **File operations**: Read, write, search, and apply diffs to files in your project
- **Shell commands**: Execute build commands, run tests, manage git -- with approval gates
- **Semantic memory**: Store and recall information across sessions using vector embeddings
- **Knowledge bases**: Build searchable vector indices from text, PDFs, or raw embeddings
- **Task tracking**: Maintain a persistent, hierarchical todo list with dependencies
- **Parallel agents**: Spawn subagents that work concurrently and report back via messaging
- **Python execution**: Run Python scripts and build custom tools that persist
- **MCP integration**: Connect to external Model Context Protocol servers
- **Web fetching**: Retrieve and process content from URLs

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

When you return for a new session, Ralph loads your recent conversation history from its vector database, generates a recap of where you left off, and automatically recalls any stored memories relevant to what you're working on.

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

One-shot mode still has access to Ralph's full memory and knowledge bases, so past context informs every response.

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

Configuration cascades: local `ralph.config.json` > `~/.local/ralph/config.json` > environment variables > defaults. Ralph auto-detects the provider from the API URL and adjusts its request format accordingly.

### Configuration Options

| Field | Description | Default |
|-------|-------------|---------|
| `api_url` | LLM API endpoint | `https://api.openai.com/v1/chat/completions` |
| `model` | Model identifier | `gpt-4o-mini` |
| `context_window` | Model context window size | `8192` (auto-detected from model) |
| `max_tokens` | Max response tokens (-1 for auto) | `-1` |
| `openai_api_key` | OpenAI API key | None |
| `anthropic_api_key` | Anthropic API key | None |
| `embedding_api_url` | Embeddings endpoint | Uses OpenAI |
| `embedding_model` | Embedding model | `text-embedding-3-small` |
| `max_subagents` | Maximum concurrent subagents | `5` |
| `subagent_timeout` | Subagent timeout in seconds | `300` |

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

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key (required for embeddings) |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `EMBEDDING_MODEL` | Override embedding model |
| `OPENAI_API_URL` | Override OpenAI endpoint |

On first run, Ralph creates a config file using any API keys found in your environment.

## Features

### Semantic Memory

Ralph's memory system is backed by a vector database (HNSWLIB) and OpenAI embeddings. When you tell Ralph to remember something, it embeds the text and stores it in a persistent semantic index. In future sessions, Ralph automatically retrieves the most relevant memories based on what you're currently discussing -- no explicit recall needed.

```bash
# Session 1: Teach Ralph about your project
> remember that our API uses snake_case for all field names
> remember that we use PostgreSQL 15 and all migrations go in db/migrate/
> remember that the billing service is maintained by the payments team
>   and changes need their review

# Session 2: Memories surface automatically when relevant
> Write a new endpoint for user preferences

[Ralph's context enhancement pipeline embeds your message, searches the
memory index, and injects the matching conventions into the system prompt.
The generated code uses snake_case fields without being told, and when it
touches billing-adjacent tables, Ralph flags it for payments team review.]
```

You can inspect and manage memories directly with slash commands:

| Command | Description |
|---------|-------------|
| `/memory list` | List all stored memories |
| `/memory search <query>` | Search memories by content |
| `/memory show <id>` | Show full details of a memory |
| `/memory edit <id> <field> <value>` | Edit a memory (re-embeds if content changes) |
| `/memory indices` | List all vector indices and their stats |
| `/memory stats` | Show index statistics |

### Knowledge Bases

Beyond simple memories, Ralph exposes 13 vector database tools that let the AI build structured, searchable knowledge bases. You can ingest PDFs, large documents, or raw text into named indices and query them semantically.

```bash
# Session 1: Build a knowledge base from your documentation
> Index our API docs and the architecture RFC into a "project_docs" knowledge base

[Ralph creates a vector index, extracts text from the PDFs, chunks them
into overlapping segments (1500 chars with 300 char overlap for PDFs),
embeds each chunk, and stores them. A 40-page PDF becomes ~187 searchable
chunks.]

# Session 2: Query the knowledge base naturally
> How does our rate limiting work? Check the project docs.

[Ralph searches the "project_docs" index with your query, retrieves the
most relevant chunks, and synthesizes an answer from the original documentation.]

# Session 3: Build on it
> Add the new auth spec to project_docs, then compare it with what we
> documented about the old auth system

[Ralph adds the new document to the same index, then runs two semantic
searches -- one for the new spec, one for old auth references -- and
produces a comparison.]
```

The AI can create purpose-specific indices (`research_papers`, `codebase_docs`, `meeting_notes`), each with independent configuration for capacity, similarity metrics, and HNSW parameters. Documents persist across sessions in `~/.local/ralph/`.

### Parallel Agents

Ralph can spawn subagents that work concurrently as separate processes. Each subagent is a full Ralph instance with access to all tools. They communicate results back to the parent through a SQLite-backed messaging system, and the parent's async executor lets you keep chatting while background work runs.

```bash
> Why is the dashboard page slow? Check the database queries, the API
> response times, and the frontend bundle size.

[Ralph spawns three subagents, each investigating one area. You can
continue the conversation while they work. As each finishes, the message
poller detects the completion notification and injects it into the
conversation -- Ralph reports findings as they arrive.]
```

**How it works under the hood:**

- Parent forks child processes via `fork()`/`exec()` with `--subagent` flag
- Each child gets a fresh conversation context with its assigned task
- Communication flows through SQLite: completion messages, direct messages, and pub/sub channels
- The parent's `select()` loop monitors stdin, notification pipes, and approval channels simultaneously
- When a subagent needs to write a file or run a shell command, the approval request proxies up to the parent, which prompts you on the TTY

**Direct messaging and channels**: Agents can send direct messages to each other and publish to shared channels. This enables coordination patterns where one agent's output feeds another's input.

Configuration: `max_subagents` (default: 5), `subagent_timeout` (default: 300s).

### Extensible Python Tools

Ralph ships with built-in Python tools for file operations, shell execution, web fetching, and directory listing. On first run, these are extracted to `~/.local/ralph/tools/`. The AI can write new tools or modify existing ones -- ask for a tool, and it becomes part of Ralph's permanent repertoire.

```bash
> I need a tool that analyzes our test coverage and highlights untested
> code paths

[Ralph writes coverage_analyzer.py to ~/.local/ralph/tools/. The tool
uses your project's docstring conventions so Ralph understands its
parameters, gate category, and approval requirements.]

# Next session, Ralph can use it autonomously
> Are there any gaps in our auth module's test coverage?

[Ralph automatically invokes the coverage_analyzer tool it created
in the previous session, alongside its built-in file search tools.]
```

**Tool discovery**: On startup, Ralph scans `~/.local/ralph/tools/` for `.py` files. Each function becomes a tool using its name, docstring, and type annotations. Gate categories (like `file_read` or `shell`) are declared via directives in the docstring, integrating custom tools with the approval system.

**Persistent interpreter**: The Python interpreter stays alive for the entire session, so tools can share state. A data-loading tool can cache results that a subsequent analysis tool reuses.

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

Ralph implements a layered security system that controls which tools can execute without asking. Every tool belongs to a category, and each category has an action: `allow` (run freely), `gate` (ask first), or `deny` (block entirely).

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

**Subagent approval proxying**: When a subagent needs approval for a gated operation, the request pipes up to the root process that owns the TTY. Nested subagents are fully transparent -- you see exactly what every agent wants to do.

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
   2.1 Update /login
   2.2 Update /logout
 3. Remove old auth code (low, blocked by 1 and 2)]

# Session 2 (days later)
> What was I working on?

[Ralph loads the task list from SQLite, sees items 1.1 and 1.2 are
complete, 1.3 is in progress. Recalls relevant memories about your
session store design decisions. Suggests picking up where you left off
on the session tests.]
```

### MCP Integration

Ralph supports the [Model Context Protocol](https://modelcontextprotocol.io/) for connecting to external tool servers. At startup, Ralph connects to each configured server, discovers available tools via JSON-RPC, and registers them with a `mcp_<server>_<tool>` naming convention to avoid collisions.

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

### AGENTS.md Support

Ralph reads `AGENTS.md` files in the current directory to customize AI behavior per project. The file supports `@FILENAME` references to include other files inline, so you can reference your architecture docs, coding standards, or API specifications directly.

```markdown
# Project Instructions

Always use our error handling pattern from @ERROR_HANDLING.md.
See @ARCHITECTURE.md for service boundaries.
Never modify files in vendor/ without approval.
```

Referenced files are wrapped in XML tags and included in the system prompt, giving Ralph project-specific context without manual copy-pasting.

### Conversation Management

Ralph automatically manages long conversations. A token manager estimates context usage, and when the conversation approaches 80% of the context window, a compactor generates a rolling summary of older messages and trims the history. The summary is injected into subsequent prompts, so context is preserved even as individual messages are dropped.

Conversation messages (excluding ephemeral tool results) are stored in the vector database, so relevant exchanges from past sessions can be retrieved semantically even after compaction.

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

### Building Project Knowledge Across Sessions

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

[Ralph recalls the hexagonal architecture convention, creates the port
interface first, then the adapter. It searches the "specs" knowledge
base to check if notifications are already defined in the OpenAPI spec,
finds they are, and implements the endpoint to match the existing
contract. All without being reminded of any of these conventions.]

# Session 3: A new team member uses Ralph
> How does our API handle authentication? Where would I add a new
>   protected endpoint?

[Ralph searches the specs knowledge base and its memories, then walks
the developer through the auth middleware, the port/adapter pattern,
and where to add the new route -- drawing from accumulated project
knowledge rather than just reading files.]
```

### Custom Tools That Compound

```bash
# Session 1: Build a tool for your workflow
> I keep manually checking if our database migrations are reversible.
>   Build me a tool that analyzes a migration file and reports whether
>   the down migration properly reverses the up migration.

[Ralph writes migration_checker.py with schema-aware analysis]

# Session 2: The tool integrates with other capabilities
> Review PR #42 for any database issues

[Ralph reads the PR diff, notices migration files, automatically invokes
the migration_checker tool it built previously, and combines the results
with its memory of your database conventions to give a comprehensive
review. The custom tool, the memory system, and the built-in file tools
all work together.]

# Session 3: Extend the tool
> The migration checker should also verify that new indices have
>   concurrent creation enabled. Update it.

[Ralph reads the existing tool, understands the structure, adds the
new check. The enhanced tool is immediately available.]
```

### Parallel Investigation With Background Work

```bash
> Our API response times jumped 3x after yesterday's deploy. Figure out
> what changed. While you're investigating, I need to prep the rollback
> script just in case.

[Ralph spawns subagents: one diffs yesterday's deploy commits, another
profiles the hot paths, a third checks database query plans. Meanwhile,
you work with Ralph on the rollback script. Results interrupt as they
arrive:]

[Subagent 1: "Commit abc123 added an N+1 query in the order listing
endpoint -- loads shipping addresses one at a time instead of batching."]

> Found it. Fix the N+1 query, and once the tests pass, add a memory
> that the orders endpoint needs the shipping_addresses join.

[Ralph fixes the query, runs tests in the background, and stores the
architectural note in semantic memory. Next time anyone touches the
orders endpoint, that context surfaces automatically.]
```

### Scripting With JSON Output

```bash
# Machine-readable output for CI/CD integration
./ralph --json "List all TODO comments in src/" | jq '.result'

# Structured code review in a pipeline
git diff origin/main...HEAD | ./ralph --json "Review for bugs" | jq '.result'
```

The `--json` flag outputs newline-delimited JSON objects, each with a `type` field (`assistant`, `user`, `system`, or `result`) for structured parsing.

### Building a Domain-Specific Research Assistant

```bash
# Session 1: Ingest research materials
> Create a "papers" knowledge base. Index these PDFs:
>   @/research/transformer_attention.pdf
>   @/research/sparse_mixture_of_experts.pdf
>   @/research/flash_attention_v2.pdf

[Ralph extracts text from each PDF, chunks with overlap to preserve
context across boundaries, embeds each chunk, and stores them in a
dedicated "papers" vector index.]

# Session 2: Cross-reference across documents
> Compare how Flash Attention and the original Transformer paper
> describe the attention computation. What did Flash Attention change?

[Ralph runs two semantic searches against the "papers" index --
one for "attention computation" and one for "Flash Attention optimization."
Retrieves relevant chunks from both papers and synthesizes a comparison.]

# Session 3: Connect research to implementation
> Based on what the papers say, review our attention implementation
> in src/model/attention.py for potential optimizations

[Ralph searches the papers knowledge base for optimization techniques,
reads your implementation, and suggests specific changes grounded in
the indexed research -- not generic advice, but recommendations tied
to the actual papers you uploaded.]
```

## Data Storage

Ralph stores persistent data in `~/.local/ralph/`:

| Path | Format | Purpose |
|------|--------|---------|
| `ralph.db` | SQLite | Tasks, messages, work queue items |
| `*.idx` / `*.meta` | HNSWLIB binary + JSON | Vector database indices |
| `documents/` | JSON files | Full document content per index |
| `metadata/` | JSON files | Chunk metadata per index |
| `tools/` | Python files | Custom and built-in Python tools |

All vector indices, messages, and tasks persist across sessions. The messaging system cleans up per-agent messages when agents exit, but channel history and stored documents remain.

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
