# Interactive Mode

Launch scaffold without arguments to enter interactive mode:

```bash
./scaffold
```

In interactive mode, you have a conversation with the orchestrator. Describe what you want, refine the spec, answer clarifying questions, and watch it build.

## Slash commands

Slash commands give you direct access to scaffold's internal state without going through the LLM. Type them at the prompt.

### `/goals` -- Goal progress

View the GOAP goal tree with completion tracking.

```
> /goals
Goals:
  abc123  ACTIVE   [3/5]  Set up Express server with routes  (supervisor)
  def456  PLANNING [0/4]  Build React frontend
  ghi789  PENDING  [0/3]  Deploy to production
```

Show details for a specific goal:

```
> /goals abc123
Goal: Set up Express server with routes
Status: ACTIVE
Progress: 3/5 assertions satisfied
Supervisor: PID 12345

World State:
  express_installed: true
  routes_defined: true
  middleware_configured: true
  database_connected: false
  tests_passing: false

Action Tree:
  [completed] Install dependencies
  [completed] Create route handlers
  [completed] Add middleware
  [running]   Connect database
  [pending]   Write integration tests
```

Goal IDs support prefix matching -- you don't need to type the full ID.

### `/tasks` -- Task queue

View the task queue and worker assignments.

```
> /tasks
In Progress:
  task_1  [high]  Implement user authentication endpoint

Pending:
  task_2  [med]   Add input validation to routes
  task_3  [low]   Write API documentation

Completed:
  task_4  [med]   Set up project structure
```

Subcommands:
- `/tasks list` -- List all tasks grouped by status
- `/tasks ready` -- Show unblocked tasks ready for work
- `/tasks show <id>` -- Show task details including blockers

### `/agents` -- Active agents

Monitor running supervisors and workers.

```
> /agents
Supervisors:
  goal_abc123  PID 12345  uptime: 2m 30s

Subagents:
  [running]    agent_1  45s   Implementing auth middleware
  [completed]  agent_2  1m    Created database schema
  [running]    agent_3  12s   Writing unit tests
```

Subcommands:
- `/agents list` -- List all agents with status
- `/agents show <id>` -- Show agent details including output and errors

### `/memory` -- Long-term memory

Manage scaffold's persistent semantic memory.

```
> /memory list
Chunks in 'long_term_memory':
  1: [user_preference] "Use TypeScript for all new files"
  2: [fact] "Project uses PostgreSQL 15"
  3: [instruction] "Always run tests before committing"
```

Subcommands:
- `/memory list [index]` -- List memory chunks
- `/memory search <query>` -- Semantic search across memories
- `/memory show <id>` -- Show full chunk details with metadata
- `/memory edit <id> <field> <value>` -- Edit chunk metadata (fields: `type`, `source`, `importance`, `content`)
- `/memory indices` -- List all vector indices with capacity stats
- `/memory stats [index]` -- Show index statistics
- `/memory help` -- Show help

When you edit the `content` field, scaffold automatically re-embeds the chunk so semantic search stays accurate.

### `/model` -- Model selection

View or switch the active LLM model.

```
> /model
Current model: gpt-4o (standard tier)

> /model list
  simple:    o4-mini
  standard:  gpt-4o  [active]
  high:      gpt-5.2-2025-12-11

> /model high
Switched to gpt-5.2-2025-12-11 (high tier)
```

### `/mode` -- Behavioral modes

Switch scaffold's behavioral mode to change how it approaches tasks.

```
> /mode
Current mode: default

> /mode list
  default  General-purpose assistant
  plan     Plan and structure before acting
  explore  Read and understand code without modifying
  debug    Diagnose and fix bugs systematically
  review   Review code for correctness and quality

> /mode debug
Switched to debug mode
```

**Mode descriptions:**

| Mode | Behavior |
|------|----------|
| `default` | General-purpose -- no behavioral overlay |
| `plan` | Breaks tasks into steps, identifies files to change, calls out risks, asks clarifying questions |
| `explore` | Reads broadly, traces call chains, reports findings with file:line references, does NOT modify files |
| `debug` | Reproduces problems first, forms and tests hypotheses, checks error messages, fixes root causes |
| `review` | Checks for memory leaks, null derefs, buffer overflows, error handling gaps, thread safety, logic errors |

## Image attachments

Attach images to your messages using the `@` prefix:

```
> Describe what's in this screenshot @/path/to/screenshot.png
```

Supported formats: PNG, JPEG, GIF, WebP.

The image is base64-encoded and sent to the LLM alongside your message. This requires a model that supports vision (most modern models do).

## Session controls

| Action | Key/Command |
|--------|-------------|
| Exit | `quit`, `exit`, or `Ctrl+D` |
| Cancel current operation | `Ctrl+C` |
| Debug HTTP traffic | Launch with `--debug` flag |

## Status line

The bottom of the terminal shows a status line with:
- Token usage (current/max)
- Active agent count
- Current behavioral mode
- Busy/idle state
