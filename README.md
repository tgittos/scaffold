# scaffold

Remember when you did your first `rails generate scaffold...`? Wasn't it magical?

This is that, but for entire applications.

```bash
wget https://github.com/tgittos/scaffold/releases/latest/download/scaffold
chmod +x scaffold
./scaffold "build a minimal twitter clone as a SPA web app. use something like React or Vue \
with a clean, responsive ui and an express backend with sqlite storage. users should be able \
to sign up, log in, post short text messages, follow/unfollow other users, and see a timeline\
 of posts from people they follow. I want a user profile page showing their posts and \
follower/following counts. JWT for auth, hash passwords with bcrypt, and keep the schema \
simple — users, posts, and follows tables. no email verification or real-time updates. \
focus on clean code, proper error handling, and a polished UI with loading states.
seed the database with a handful of demo users and posts so the app feels alive on \
first launch."
```

Describe what you want. Walk away. Come back to working software.

Built on [Cosmopolitan C](https://github.com/jart/cosmopolitan) because I think it's neat.

---

## How it works

scaffold orchestrates a three-layer hierarchy of AI agents to plan and build software autonomously.

**The orchestrator** takes your description, figures out the overall architecture, and breaks it into a sequence of supervised goals. It doesn't write a single line of code itself — it plans and delegates.

**Goal supervisors** are long-lived agents that own a single goal. Each supervisor breaks its goal into discrete tasks, dispatches worker agents to execute them, checks the results, and decides what's next. When all tasks for a goal are complete, the supervisor reports back to the orchestrator and exits.

**Workers** do the actual work. Write code. Run tests. Fix the bug the tests found. Install dependencies. Each worker handles one task and exits. They're fast, focused, and disposable.

The orchestrator moves to the next goal when the previous one is complete. Repeat until the application exists. It's ralphs all the way down.

---

## What it can do

- Anthropic/OpenAI-compatible providers, including local models via LM Studio or Ollama
- Vision support — attach images to conversations for multimodal models
- Extended thinking for models that expose reasoning (Claude, Qwen, DeepSeek)
- Python-based filesystem, shell, and web tools out of the box
- Semantic long-term memory across sessions via HNSWLIB vector store
- Task tracking and management persisted to SQLite
- MCP client with stdio, HTTP, and SSE transports, multi-server support, per-server tool namespacing
- Sub-agents with direct and pub/sub messaging
- Approval gates with allowlists, protected file detection, shell command parsing, and path traversal prevention
- Project-aware via AGENTS.md context loading
- Self-updater via GitHub releases

---

## How to use

### REPL mode

Talk directly to the orchestrator. Develop your specification interactively — describe the application, refine goals, answer clarifying questions. When the spec is ready, tell it to build.

```bash
./scaffold
```

If things go sideways, drop into the slash commands to see what's happening:

- `/goals` — current goal state
- `/tasks` — task queue and status
- `/agents` — active agents
- `/memory` — what it remembers
- `/mode` — current operating mode
- `/model` — which LLM is in use

### One-shot mode

Pipe a complete spec and let it run unattended:

```bash
cat ./SOFTWARE_SPEC.md | ./scaffold
```

The orchestrator will plan and execute the full spec and continue until it's done.

Both modes require tool approval by default. Bypass entirely with `--yolo`.

---

## Building

scaffold is written in C/C++ targeting the Cosmopolitan compiler. Dependencies:

- libcurl
- ncurses
- mbedtls
- readline
- zlib
- hnsw
- pdfio
- cjson
- ossp-uuid
- Python
- SQLite

### devcontainer

Rather than setting up a Cosmopolitan toolchain from scratch, use the devcontainer:

```bash
# Build the devcontainer image
docker build -t scaffold-dev .devcontainer/

# Build the project
docker run --rm --privileged \
    -v /your/checkout/path:/workspaces/ralph \
    -w /workspaces/ralph \
    scaffold-dev \
    ./scripts/build.sh
```

The `--privileged` flag is required to register the APE binary loader for Cosmopolitan executables.
