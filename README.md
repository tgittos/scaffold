# scaffold

Most AI coding tools are fancy autocomplete. You type, they suggest. You chat, they respond. You're still driving.

Scaffold doesn't assist. It builds.

```bash
wget https://github.com/tgittos/scaffold/releases/latest/download/scaffold
chmod +x scaffold
./scaffold --login
./scaffold "build a minimal twitter clone as a SPA web app. use something like React or Vue \
with a clean, responsive ui and an express backend with sqlite storage. users should be able \
to sign up, log in, post short text messages, follow/unfollow other users, and see a timeline \
of posts from people they follow. I want a user profile page showing their posts and \
follower/following counts. JWT for auth, hash passwords with bcrypt, and keep the schema \
simple — users, posts, and follows tables. no email verification or real-time updates. \
focus on clean code, proper error handling, and a polished UI with loading states. \
seed the database with a handful of demo users and posts so the app feels alive on \
first launch."
```

Describe what you want. Walk away. Come back to working software.

> **Alpha software.** Scaffold works, but it has rough edges. If you're the kind of person who runs code from a README without flinching, you're in the right place.

---

## How it works

Scaffold operates like a small software team, not a single chatbot.

When you give it a description, it doesn't start writing code. It plans. It breaks your spec into goals, assigns each goal a supervisor, and the supervisors dispatch specialized workers to do the actual building. The whole thing runs autonomously — no hand-holding, no approval loops (unless you want them).

**The orchestrator** reads your description and decomposes it into a sequence of goals. It figures out the architecture, decides what to build first, and manages the pipeline. It never writes a line of code itself.

**Goal supervisors** are long-lived agents, each responsible for a single goal. A supervisor uses [GOAP (Goal-Oriented Action Planning)](https://en.wikipedia.org/wiki/Goal-oriented_action_planning) to reason about what's done and what's next. It tracks world state — a set of boolean assertions about what exists so far — and dispatches workers until the goal state is satisfied. If a worker fails, the supervisor adapts. If a supervisor crashes, it recovers from its last known state.

**Workers** are fast, focused, and disposable. Each one handles a single task — write a migration, implement an endpoint, run the tests, fix the bug the tests found — then exits. Workers are assigned roles (implementation, testing, code review, architecture review, design review, PM review), each with a tailored system prompt that keeps them on task.

The orchestrator advances to the next goal when the current one is verified complete. Repeat until the application exists.

It's [ralphs](https://ghuntley.com/ralph/) all the way down.

---

## Quick start

### 1. Install

```bash
wget https://github.com/tgittos/scaffold/releases/latest/download/scaffold
chmod +x scaffold
```

### 2. Configure

```bash
# Use your ChatGPT subscription (recommended — flat rate, no per-token fees)
./scaffold --login

# Or use an API key
export OPENAI_API_KEY="sk-..."
# or
export ANTHROPIC_API_KEY="sk-ant-..."
```

`--login` opens a browser, authenticates via OpenAI OAuth, and stores your session locally. After that, scaffold routes through the Codex API using your ChatGPT subscription — no API key needed, no per-token billing. `--logout` when you're done.

> **Note:** Semantic memory requires an API key for embeddings even when using Codex login. Set `OPENAI_API_KEY` alongside `--login` if you want long-term memory across sessions.

### 3. Run

```bash
# Interactive mode
./scaffold

# One-shot autonomous mode
./scaffold "build a todo app with React and Express"

# Pipe a spec file
cat SPEC.md | ./scaffold

# Select a specific model
./scaffold --model gpt-4o "build a blog with Next.js"

# Full autonomy (no approval prompts)
./scaffold --yolo "build a blog with Next.js"
```

See the [Getting Started guide](docs/getting-started.md) for more details.

---

## Why this exists

Every AI coding tool today puts you in the driver's seat. Copilot suggests lines. Cursor chats about files. Claude Code runs commands you approve. They're all built around the same assumption: the human is the architect, the AI is the assistant.

Scaffold inverts this. You're the client. Scaffold is the team.

You provide the spec. Scaffold provides the architect, the developers, the testers, and the project manager. Its plans aren't hidden in an LLM's context window — they're explicit GOAP goal trees, persisted to SQLite, queryable with `/goals`. You can watch it think.

This isn't a better copilot. It's a different thing entirely.

---

## Where this is going

Right now, scaffold generates applications from descriptions. That's the starting point, not the destination.

The ambition is a software factory. Point it at a product backlog. Feed it a roadmap. Let it build your product while you sleep. Come back in the morning, review the PRs, ship.

One person with scaffold should be able to build and maintain what used to take a team.

---

## What's under the hood

Scaffold is a single binary. No runtime dependencies. No Docker. No Python install. No npm. Download it and run it.

This is possible because it's written in C and compiled with [Cosmopolitan](https://github.com/jart/cosmopolitan), which produces Actually Portable Executables — one binary that runs on Linux, macOS, Windows, FreeBSD, and OpenBSD across x86_64 and ARM64. The Python interpreter, the vector database, the PDF processor, the TLS stack — all compiled in.

### Capabilities

- **LLM providers**: Anthropic, OpenAI, Codex (ChatGPT subscription via OAuth), and anything API-compatible (LM Studio, Ollama, etc.)
- **Codex auth**: Log in with your ChatGPT subscription instead of paying per-token. OAuth2 with PKCE, encrypted token storage, automatic refresh.
- **Extended thinking**: Native support for reasoning models (Claude, Qwen, DeepSeek)
- **Prompt caching**: Split system prompts for Anthropic and OpenAI to maximize cache hits, reducing latency and cost
- **Vision**: Attach images to conversations for multimodal models
- **Tools**: 54 built-in tools — file I/O, shell, web fetch, PDF processing, package management
- **Memory**: Semantic long-term memory via HNSWLIB vector store, persisted across sessions
- **MCP**: Model Context Protocol client with stdio, HTTP, and SSE transports
- **Plugins**: JSON-RPC 2.0 plugin protocol — drop executables into `~/.local/scaffold/plugins/`, register custom tools in any language
- **Sub-agents**: Fork/exec process spawning with inter-agent messaging (direct + pub/sub)
- **Task persistence**: Goals, actions, and work queues backed by SQLite
- **Self-update**: Checks GitHub releases and updates in place
- **Project awareness**: Reads AGENTS.md for project-specific context

---

## Documentation

| Guide | Description |
|-------|-------------|
| [Getting Started](docs/getting-started.md) | Installation, first run, basic usage |
| [Configuration](docs/configuration.md) | Config file, environment variables, API providers, approval gates |
| [Interactive Mode](docs/interactive-mode.md) | Slash commands, image attachments, behavioral modes |
| [Autonomous Mode](docs/autonomous-mode.md) | How the orchestrator, supervisors, and workers operate |
| [Memory System](docs/memory.md) | Long-term semantic memory storage and retrieval |
| [Custom Tools](docs/custom-tools.md) | Adding Python tools and customizing worker role prompts |
| [MCP Servers](docs/mcp.md) | Connecting external tool servers via Model Context Protocol |
| [Plugins](docs/plugins.md) | Building and installing plugins with the JSON-RPC 2.0 protocol |
| [CLI Reference](docs/cli-reference.md) | Complete command-line flag reference |
| [Building from Source](docs/building-from-source.md) | Compiling, testing, and debugging |

---

## Building from source

Scaffold targets the Cosmopolitan toolchain. The easiest path is the devcontainer:

```bash
# Build the devcontainer image
docker build -t scaffold-dev .devcontainer/

# Build scaffold
docker run --rm --privileged \
    -v /your/checkout/path:/workspaces/ralph/scaffold \
    -w /workspaces/ralph/scaffold \
    scaffold-dev \
    ./scripts/build.sh
```

The `--privileged` flag registers the APE binary loader for Cosmopolitan executables.

### Dependencies (vendored or linked at build time)

libcurl, mbedtls, readline, ncurses, zlib, cJSON, SQLite, HNSWLIB, PDFio, ossp-uuid, Python 3.12

### Running tests

```bash
./scripts/build.sh && ./scripts/run_tests.sh
```

See [Building from Source](docs/building-from-source.md) for the full guide.
