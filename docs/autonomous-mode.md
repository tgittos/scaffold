# Autonomous Mode

Scaffold's autonomous mode is what makes it different from other AI coding tools. Instead of assisting you line by line, it operates like a small software team -- planning, building, testing, and reviewing without intervention.

## Architecture

Three layers of agents coordinate to build your software:

```
Orchestrator (you talk to this)
  |
  +-- Goal Supervisor (one per goal, long-lived)
  |     |
  |     +-- Worker (implementation)
  |     +-- Worker (testing)
  |     +-- Worker (code review)
  |
  +-- Goal Supervisor
        |
        +-- Worker (implementation)
        +-- Worker (architecture review)
```

### The orchestrator

The orchestrator is the top-level agent. When you describe what you want, it:

1. Decomposes your description into a sequence of **goals**
2. Defines explicit **completion criteria** for each goal (boolean assertions like "routes_defined: true")
3. Starts each goal's **supervisor** in sequence
4. Advances to the next goal when the current one is verified complete

The orchestrator never writes code. It plans and manages.

### Goal supervisors

Each goal gets a dedicated supervisor process. Supervisors use [GOAP (Goal-Oriented Action Planning)](https://en.wikipedia.org/wiki/Goal-oriented_action_planning) to reason about progress:

- **Goal state**: A set of boolean assertions that define "done" (e.g., `{"database_schema_exists": true, "migrations_run": true, "tests_passing": true}`)
- **World state**: The current reality -- which assertions are satisfied
- **Actions**: Steps to take, with preconditions and effects

The supervisor dispatches workers until the world state satisfies the goal state. If a worker fails, the supervisor adapts -- it can retry, try a different approach, or escalate.

Supervisors persist their state to SQLite. If a supervisor crashes, it recovers from its last known state and resumes where it left off.

### Workers

Workers are fast, focused, and disposable. Each one handles a single task:

- Write a migration
- Implement an endpoint
- Run the tests
- Fix the bug the tests found
- Review the code for security issues

Workers are assigned **roles** with specialized system prompts. When a worker finishes, it reports back to its supervisor and exits.

## Worker roles

Six built-in roles shape how workers approach their tasks:

| Role | Behavior |
|------|----------|
| `implementation` | Build and modify code. Read existing patterns first. Ensure memory safety. Test changes. No TODOs or placeholders. |
| `testing` | Write and run tests. Cover happy path, edge cases, and error conditions. Follow existing test patterns. |
| `code_review` | Review for quality, security, and correctness. Check memory safety, injection vulnerabilities, error handling. Read-only. |
| `architecture_review` | Evaluate module boundaries, dependencies, coupling. Check for circular dependencies and abstraction leaks. Read-only. |
| `design_review` | Assess UX/UI decisions, API surface design, data model choices. Check for consistency and completeness. Read-only. |
| `pm_review` | Verify implementation matches requirements. Check edge cases, error states, and acceptance criteria. Read-only. |

### Custom role prompts

Override built-in roles or create new ones by placing markdown files in:

```
~/.local/scaffold/prompts/<role>.md
```

For example, `~/.local/scaffold/prompts/implementation.md` would override the built-in implementation role prompt. Role names must be alphanumeric with hyphens and underscores.

## GOAP in detail

### Goals

A goal is a named objective with explicit completion criteria:

```json
{
  "name": "Set up authentication",
  "description": "Implement JWT-based auth with login, signup, and protected routes",
  "goal_state": {
    "auth_middleware_exists": true,
    "login_endpoint_works": true,
    "signup_endpoint_works": true,
    "protected_routes_gated": true,
    "auth_tests_passing": true
  }
}
```

### Actions

Actions are the steps to achieve a goal. They can be **compound** (containing sub-actions) or **primitive** (dispatched to a worker):

```
[compound] Implement auth system
  [primitive] Create user model and migration
  [primitive] Implement JWT token generation
  [primitive] Build login endpoint
  [primitive] Build signup endpoint
  [primitive] Add auth middleware
[compound] Verify auth system
  [primitive] Write auth unit tests
  [primitive] Run integration tests
  [primitive] Code review auth module
```

Actions have:
- **Preconditions**: World state assertions that must be true before the action can run
- **Effects**: World state assertions that become true when the action completes
- **Role**: Which worker role executes it (implementation, testing, etc.)

### World state

World state is a set of boolean assertions that track what exists:

```json
{
  "user_model_exists": true,
  "jwt_generation_works": true,
  "login_endpoint_works": true,
  "signup_endpoint_works": false,
  "auth_middleware_exists": false,
  "auth_tests_passing": false
}
```

Supervisors update world state as workers complete actions. When world state satisfies goal state, the goal is complete.

### Persistent goals

Goals can be marked as **persistent**, meaning they stay active across supervisor wake cycles. This is useful for ongoing objectives like "keep tests passing" or "maintain code quality".

### Summary notes

Supervisors can attach summary notes to goals for context recovery. When a supervisor restarts after a crash, it reads the summary to understand what happened before.

## Monitoring autonomous execution

While scaffold is building, use slash commands to watch progress:

```bash
# See all goals and their completion percentage
/goals

# Deep-dive into a specific goal's action tree
/goals <id>

# See what workers are doing right now
/agents

# Check the task queue
/tasks
```

## Concurrency

- Multiple workers can run in parallel within a goal (up to `max_workers_per_goal`, default 3)
- Goals execute sequentially by default (the orchestrator advances one at a time)
- Workers are isolated processes -- they don't share memory with each other
- Inter-agent communication happens through a SQLite-backed message store

## Failure handling

- **Worker failure**: The supervisor is notified and can retry, dispatch a different worker, or adapt its plan
- **Supervisor crash**: Recovers from SQLite-persisted state and resumes
- **Orphaned actions**: On startup, supervisors detect and recover actions that were running when a previous instance crashed
- **Stale PIDs**: The orchestrator detects dead supervisor processes and cleans up
