# scaffold

Remember when you did your first `rails generate scaffold...`? Wasn't it magical?

Wouldn't it be cool to do that, but for whole applications?

## Usage

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

Built on [Cosmopolitan C](https://github.com/jart/cosmopolitan) because I think it's neat.

## How it works

`scaffold` is my version of [Gastown](https://steve-yegge.medium.com/welcome-to-gas-town-4f25ee16dd04).

It's built on my version of [ralph](https://ghuntley.com/ralph/).

If all of that is nonsense and you don't want to read two very long blog posts, in a nutshell:

`scaffold` flips the agent/task paradigm and effectively plans and executes software specs of varying degrees of detail by orchestrating long-lived AI agent supervised goals. Each goal is broken down into a series of tasks, which in turn are worked by autonomous AI agents until the overall goal is complete.

It's 3 layers of abstraction in a trenchcoat, with the final layer being mostly stacks of ralphs in a trenchcoat.

### libagent

A generic-ish library for agentic AI stuff:

- Anthropic/OpenAI-compatible provider support (including local via LM Studio, Ollama)
- async tool calling with built-in filesystem, shell, memory, PDF, vector DB, and task tools
- extensible Python tool system
- MCP tool support (stdio and HTTP/SSE transports)
- sub-agents with direct and pub/sub messaging
- GOAP-based multi-agent orchestration with work queues
- HNSWLIB-backed semantic memory and document store
- conversation management with token-aware compression and rolling summaries
- category-based approval gates with allowlists and protected file detection
- SSE streaming with provider-specific parsing
- SQLite-backed persistence for tasks, messages, goals, and actions
- self-updater via GitHub releases
- AGENTS.md support

Used to build my ralph. Use it to build a ralph of your own.

### ralph

All the kids are ralphin', it's the coolest.

This is my ralph.

Mine is a little smarter than the average ralph:

- interactive REPL and single-shot CLI modes
- Python-based filesystem, shell, and web tools out of the box
- semantic long-term memory across sessions
- task tracking and management
- project-aware via AGENTS.md context loading

### scaffold

An orchestrator ralph that co-ordinates long lived goal supervisor ralphs, who each dispatch ralphs to work individual tasks.

It's ralphs all the way down.

## How to use

That intro "Usage" section was a little glib. How do you actually productively `scaffold` applications?

`scaffold` supports two primary methods of interaction - an interactive chat-based REPL and a pipeable and stdin/stdout compatible CLI mode.

In both modes, by default all tools will require user approval.

Bypass entirely with `--yolo`.

### REPL mode

Talk directly to the orchestrator. Chat and develop a software specification interactively. Once you have a spec, ask it to scaffold. Direct the orchestrator to refine goals, modify tasks and check on the progress of agents.

If the orchestrator is giving you a hard time, drop into the various slash commands to investigate manually:

- /memory
- /goals
- /tasks
- /agents
- /mode
- /model

### One-shot mode

Send a complete software specification into `scaffold` with:

```bash
cat ./SOFTWARE_SPEC.md | ./scaffold
```

The orchestrator will autonomously plan and execute your software design, and continue working until it has completed.

## Building

`scaffold` and co are written in C/C++ targeting the Cosmopolitan compiler. It has the following dependencies that are also downloaded and compiled:

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

Rather than having to setup a Cosmopolitan toolchain of your own, you can build this project using the devcontainer:

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