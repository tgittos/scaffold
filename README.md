# scaffold

Scaffold is an autonomous coding agent. You give it a description of an application, and it plans the work, writes the code, runs the tests and fixes what breaks — approval prompts only if you want them.

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

> **Alpha software.** It works, but there are rough edges. If you're comfortable running a binary straight off a README, you'll be fine.

---

## How it works

Scaffold splits the work across three kinds of agent rather than running one long chat.

**The orchestrator** reads your description and breaks it into a sequence of goals. It decides the architecture and the build order, and it doesn't write code itself.

**Goal supervisors** each own one goal. A supervisor uses [GOAP (Goal-Oriented Action Planning)](https://en.wikipedia.org/wiki/Goal-oriented_action_planning) to track what's done and what's next — world state is a set of boolean assertions about what exists so far — and keeps dispatching workers until the goal state holds. When a worker fails, the supervisor re-plans. When a supervisor crashes, it resumes from its last saved state.

**Workers** handle one task each (write a migration, implement an endpoint, run the tests, fix what the tests found) and then exit. Every worker has a role — implementation, testing, code review, architecture review, design review or PM review — with its own system prompt.

Once a goal is verified, the orchestrator moves on to the next one, until there are none left. It's [ralphs](https://ghuntley.com/ralph/) all the way down.

---

## Quick start

### 1. Install

```bash
wget https://github.com/tgittos/scaffold/releases/latest/download/scaffold
chmod +x scaffold
```

### 2. Configure

```bash
# Use your ChatGPT subscription (flat rate, no per-token fees)
./scaffold --login

# Or use an API key
export OPENAI_API_KEY="sk-..."
# or
export ANTHROPIC_API_KEY="sk-ant-..."
```

`--login` opens a browser, authenticates with OpenAI OAuth and stores the session locally. From then on scaffold talks to the Codex API on your ChatGPT subscription, so there's no API key or per-token billing. `--logout` clears it.

> **Note:** semantic memory needs an embeddings endpoint, even with Codex login. Either set `OPENAI_API_KEY`, or point `embedding_api_url` and `embedding_model` in your config at a local embedding server (no key needed). See [Memory System](docs/memory.md).

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

# No approval prompts
./scaffold --yolo "build a blog with Next.js"
```

The [Getting Started guide](docs/getting-started.md) goes into more detail.

---

## Why this exists

Most AI coding tools assume you're the architect and the AI is helping. Copilot suggests lines, Cursor chats about files, Claude Code runs commands you approve.

Scaffold takes the other side of that: you write the spec, and it does the architecture, development, testing and project management. Its plan isn't buried in a context window either. Goals are explicit GOAP trees persisted to SQLite, and `/goals` shows you where it's up to.

---

## Where this is going

Generating an application from a description is the starting point. The longer-term aim is something closer to a software factory — point it at a backlog or a roadmap, let it work overnight, and review the PRs in the morning.

---

## What's under the hood

Scaffold is a single binary with no runtime dependencies, so there's no Docker, Python or npm to install.

It's written in C and compiled with [Cosmopolitan](https://github.com/jart/cosmopolitan), which produces Actually Portable Executables: one binary that runs on Linux, macOS, Windows, FreeBSD and OpenBSD, on x86_64 and ARM64. The Python interpreter, vector database, PDF processor and TLS stack are all compiled in.

### Capabilities

- **LLM providers**: Anthropic, OpenAI, Codex (ChatGPT subscription via OAuth), and anything API-compatible (LM Studio, Ollama, etc.)
- **Codex auth**: OAuth2 with PKCE, encrypted token storage and automatic refresh
- **Extended thinking**: native support for reasoning models (Claude, Qwen, DeepSeek)
- **Prompt caching**: system prompts are split for Anthropic and OpenAI to get more cache hits
- **Vision**: attach images for multimodal models
- **Tools**: 54 built-in tools covering file I/O, shell, web fetch, PDF processing and package management
- **Memory**: semantic long-term memory in an HNSWLIB vector store, persisted across sessions
- **MCP**: Model Context Protocol client with stdio, HTTP and SSE transports
- **Plugins**: JSON-RPC 2.0 plugin protocol — drop executables into `~/.local/scaffold/plugins/` to register tools in any language
- **Sub-agents**: fork/exec process spawning with inter-agent messaging (direct and pub/sub)
- **Task persistence**: goals, actions and work queues stored in SQLite
- **Self-update**: checks GitHub releases and updates in place
- **Project awareness**: reads `AGENTS.md` for project-specific context

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

Scaffold builds with the Cosmopolitan toolchain, and the devcontainer is the easiest way to get it:

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

`--privileged` lets the container register the APE binary loader for Cosmopolitan executables.

### Dependencies (vendored or linked at build time)

libcurl, mbedtls, readline, ncurses, zlib, cJSON, SQLite, HNSWLIB, PDFio, ossp-uuid, Python 3.12

### Running tests

```bash
./scripts/build.sh && ./scripts/run_tests.sh
```

The full guide is in [Building from Source](docs/building-from-source.md).
