# Scaffold: Goal-Oriented Orchestration

## Context

Scaffold is an orchestrator built on ralph. Three layers:

- **libagent** (`libagent.a`): Generic agent library — tool calling, sub-agents, messaging, memory, MCP, LLM providers.
- **ralph** (`src/ralph/main.c`): A standalone AI agent binary with semantic memory, Python tooling, MCP support, task tracking.
- **scaffold** (`src/scaffold/main.c`): An orchestrator binary that coordinates goal supervisors and their worker agents.

It's ralphs all the way down.

Both binaries link against `libagent.a`. `ralph` is the standalone agent. `scaffold` is the orchestrator that manages long-lived goals using ephemeral workers.

**Core insight**: Flip the worker/work relationship. Work (goals, actions, results, world state) is persistent and lives in SQLite stores. Workers are ephemeral processes that pick up an action, execute it with a fresh context window, report results, and die. Goal supervisors maintain lean context (goal state + world state + action table) and never accumulate worker conversation histories.

**Planning model**: Combines Goal-Oriented Action Planning (GOAP) with Hierarchical Task Network (HTN) decomposition. Goals are desired world states. Actions are transforms with preconditions and effects — dependencies emerge mechanically from precondition/effect chains. Actions can be **compound** (decomposed into children just-in-time by the supervisor) or **primitive** (dispatched to workers). This hierarchical decomposition means the LLM only needs to plan one level of detail at a time, with full context from completed prerequisite work.

## Architecture

Every process in scaffold is a ralph instance running in a different mode.

```
scaffold (interactive REPL or one-shot mode)
  └── Orchestrator (in-process, manages goals)
        ├── Goal A ──→ ralph --supervisor --goal A
        │                ├── ralph --worker --queue A (action 1) → dies
        │                ├── ralph --worker --queue A (action 2) → dies
        │                └── ralph --worker --queue A (action 3) → dies
        └── Goal B ──→ ralph --supervisor --goal B
                         └── ralph --worker --queue B (action 4) → dies
```

- **Scaffold**: The user-facing process. In REPL mode, it's an interactive ralph with orchestrator tools and slash commands. In one-shot mode (`cat spec.md | ./scaffold` or `./scaffold "build X"`), it receives the spec, decomposes into goals, starts supervisors, monitors to completion, and exits. One-shot mode is a natural consequence of ralph's existing one-shot mode — the orchestrator module just needs to be active.
- **Orchestrator**: In-process module inside scaffold. Exposes LLM tools (`create_goal`, `goal_status`, etc.) and slash commands (`/goals`, `/tasks`, `/agents`) so the user manages goals through conversation or direct inspection.
- **Goal Supervisor**: A ralph process (`--supervisor --goal <id>`) forked per active goal. Runs the GOAP planning loop: evaluate world state → find ready actions → decompose or dispatch → collect results → verify effects → update world state → repeat.
- **Worker**: An ephemeral ralph process (`--worker --queue <name>`). Claims a work item, runs `session_process_message()` with fresh conversation history, reports the result string, exits.

### Future: HTTP Control Surface

The supervisor's GOAP engine is deliberately decoupled from UI code — no session dependencies, no terminal assumptions. This makes it a natural target for an HTTP control surface: expose world state, action status, and commands (pause, cancel, add actions) over HTTP without refactoring the decision engine.

## GOAP Model

### World State

A set of boolean assertions about the current state of the project:

```json
{
  "project_initialized": true,
  "database_schema_exists": true,
  "auth_endpoints_functional": true,
  "frontend_renders_timeline": false
}
```

Absent keys are false. The supervisor maintains this as a JSON object, persisted on the goal row. World state is updated when workers complete primitive actions and the supervisor verifies their effects.

### Goal State

The acceptance criteria — a subset of world state assertions that must all be true:

```json
{
  "app_functional": true,
  "tests_passing": true,
  "ui_polished": true,
  "code_reviewed": true,
  "architecture_approved": true,
  "requirements_verified": true
}
```

Goal state includes both functional assertions and quality gates. A goal is complete when every key in goal_state exists and is true in world_state. This check is mechanical — no LLM call needed.

### Actions

Actions come in two kinds: **compound** and **primitive**.

- **Primitive actions** are dispatched to workers for execution. They are leaf-level units of work.
- **Compound actions** are decomposed into child actions by the supervisor when they become ready. They represent phases or high-level steps. A compound action's effects are its "contract" — the promise of what its children will collectively achieve.

Both kinds share the same struct and store. The `is_compound` flag determines the supervisor's handling, and `parent_action_id` links children to their compound parent:

```c
typedef enum {
    ACTION_STATUS_PENDING = 0,
    ACTION_STATUS_RUNNING = 1,
    ACTION_STATUS_COMPLETED = 2,
    ACTION_STATUS_FAILED = 3,
    ACTION_STATUS_SKIPPED = 4     // Superseded by replanned actions
} ActionStatus;

typedef struct {
    char id[40];                  // UUID
    char goal_id[40];             // Parent goal
    char parent_action_id[40];    // Parent compound action ("" if top-level)
    char description[512];        // What to do (or what phase this represents)
    char* preconditions;          // JSON array of assertion strings
    char* effects;                // JSON array of assertion strings
    bool is_compound;             // true = decompose, false = dispatch to worker
    ActionStatus status;
    char role[32];                // Worker role (determines system prompt)
    char* result;                 // Worker output (primitives only, set on completion)
    int attempt_count;            // For retry tracking
    time_t created_at;
    time_t updated_at;
} Action;
```

**Readiness**: An action is ready when its status is PENDING and every assertion in its preconditions array is true in the current world state. No explicit dependency edges — readiness is derived from state.

**Compound action completion**: A compound action is marked COMPLETED when all its children are COMPLETED and its effects are verified in world state. If children complete but the compound's effects aren't all satisfied, the supervisor generates additional children to close the gap — same "stuck" logic, scoped to the compound action's effects.

### Worker Roles

Each primitive action carries a `role` that determines the worker's system prompt. The supervisor maps roles to system prompt templates when spawning workers via `worker_spawn()` (which already accepts a `system_prompt` parameter).

| Role | System Prompt Focus |
|------|-------------------|
| `implementation` | Build, create, modify code. Default role for workers. |
| `code_review` | Review code for quality, security, error handling, style. Read files, check patterns, report issues. |
| `architecture_review` | Evaluate structural decisions, module boundaries, dependency patterns. |
| `design_review` | Assess UX/UI decisions, API surface design, data model choices. |
| `pm_review` | Check implementation against original requirements. Verify acceptance criteria are met. |
| `testing` | Write and run tests, verify behavior, check edge cases. |

Roles are strings, not an enum — new roles can be added without code changes. The supervisor loads prompt templates from a configurable directory (following the `python_defaults/` pattern) or falls back to a generic worker prompt for unknown roles.

Verification actions are first-class actions in the GOAP model. They have preconditions on the implementation effects they verify (so they naturally run after implementation completes) and produce quality gate effects that feed into the goal state. The decomposition prompts instruct the LLM to include verification actions alongside implementation actions.

**Example** — initial decomposition of "Build a Twitter clone":
```json
[
  {
    "description": "Set up project infrastructure",
    "is_compound": true,
    "preconditions": [],
    "effects": ["project_initialized", "build_system_configured"]
  },
  {
    "description": "Build database and backend API",
    "is_compound": true,
    "preconditions": ["project_initialized"],
    "effects": ["database_schema_exists", "auth_endpoints_functional", "api_endpoints_functional",
                "backend_code_reviewed", "auth_requirements_verified"]
  },
  {
    "description": "Build frontend application",
    "is_compound": true,
    "preconditions": ["api_endpoints_functional"],
    "effects": ["frontend_renders_timeline", "ui_polished", "frontend_code_reviewed"]
  },
  {
    "description": "Integration, review, and polish",
    "is_compound": true,
    "preconditions": ["frontend_renders_timeline", "auth_endpoints_functional",
                      "backend_code_reviewed", "frontend_code_reviewed"],
    "effects": ["tests_passing", "app_functional", "architecture_approved",
                "requirements_verified", "code_reviewed"]
  }
]
```

Note how compound phases include quality gate effects alongside functional ones. Verification actions appear naturally as children during decomposition. When "Build database and backend API" becomes ready, the supervisor decomposes it:
```json
[
  {
    "description": "Design and create SQLite schema for users, posts, follows",
    "is_compound": false, "role": "implementation",
    "preconditions": [],
    "effects": ["database_schema_exists"]
  },
  {
    "description": "Implement JWT auth with bcrypt password hashing, login/signup endpoints",
    "is_compound": false, "role": "implementation",
    "preconditions": ["database_schema_exists"],
    "effects": ["auth_endpoints_functional"]
  },
  {
    "description": "Implement CRUD API for posts, follows, and timeline",
    "is_compound": false, "role": "implementation",
    "preconditions": ["database_schema_exists", "auth_endpoints_functional"],
    "effects": ["api_endpoints_functional"]
  },
  {
    "description": "Code review: verify error handling, SQL injection prevention, JWT security",
    "is_compound": false, "role": "code_review",
    "preconditions": ["auth_endpoints_functional", "api_endpoints_functional"],
    "effects": ["backend_code_reviewed"]
  },
  {
    "description": "PM review: verify auth matches spec (JWT tokens, bcrypt, login/signup)",
    "is_compound": false, "role": "pm_review",
    "preconditions": ["auth_endpoints_functional"],
    "effects": ["auth_requirements_verified"]
  }
]
```

Implementation workers build the code. Verification workers — with role-specific system prompts — review it. Both are just primitives with preconditions and effects; the GOAP planner handles ordering mechanically. The code review can't run until implementation is done because it requires `auth_endpoints_functional` and `api_endpoints_functional` in world state.

The initial decomposition only needs 3-5 high-level compound actions. Each decomposition call is small and focused — the LLM has context from completed prerequisite work to plan the next level of detail, including which verification actions are appropriate for the work being done.

### Planning

Planning is hierarchical and just-in-time:

1. **Initial decomposition** (`create_goal`): The LLM decomposes the goal into compound actions (high-level phases) plus the goal state assertions. This is a coarse plan — 3-5 actions, not 30.
2. **Just-in-time decomposition** (supervisor loop): When a compound action becomes ready (preconditions satisfied), the supervisor asks the LLM to decompose it into children. Children can be primitive (worker-executable) or further compound (decomposed later). The supervisor has results from prerequisite actions available, so it can plan with more context than the initial decomposition had.
3. **Primitive dispatch**: When a primitive action becomes ready, it goes to a worker.
4. **Continuous replanning**: If the supervisor is stuck at any level (no ready actions, no running workers, effects not met), it asks the LLM to generate new actions bridging the gap. This works identically for top-level stuck states and for compound actions whose children didn't fully satisfy the contract.

### LLM's Role in the GOAP Loop

The LLM is used for four specific things:

1. **Initial decomposition** (in scaffold, during `create_goal`): Goal description → goal state assertions + compound action library with preconditions/effects.
2. **Compound decomposition** (in supervisor, when compound action is ready): Compound action description + world state + prerequisite results → child actions (compound or primitive) with preconditions/effects.
3. **Effect verification** (in supervisor, after worker completes a primitive): "Worker completed this action and reported: [result]. Are these effects satisfied: [effects list]?" → yes/no per effect. Verified effects are applied to world state.
4. **Action generation** (in supervisor, when stuck): "Current world state is X. Target effects are Y. No available actions can progress. What new actions would bridge the gap?" → new actions with preconditions/effects.

Goal completion is mechanical — no LLM call needed. `world_state ⊇ goal_state` → done.

## Implementation Phases

### Phase 1: Goal Store

SQLite-backed store following `task_store` / `sqlite_dal` patterns.

**New files:**
- `lib/db/goal_store.h` — `Goal` struct, CRUD API
- `lib/db/goal_store.c` — Implementation using `sqlite_dal`
- `test/db/test_goal_store.c` — Unit tests

**Goal struct:**
```c
typedef enum {
    GOAL_STATUS_PLANNING,     // Being decomposed into actions
    GOAL_STATUS_ACTIVE,       // Supervisor running
    GOAL_STATUS_PAUSED,       // Supervisor stopped, can resume
    GOAL_STATUS_COMPLETED,    // Goal state satisfied
    GOAL_STATUS_FAILED        // Unrecoverable
} GoalStatus;

typedef struct {
    char id[40];                   // UUID
    char name[128];                // Human-readable
    char* description;             // Full goal description
    char* goal_state;              // JSON object: acceptance criteria assertions
    char* world_state;             // JSON object: current state assertions
    char* summary;                 // Rolling status (supervisor updates)
    GoalStatus status;
    char queue_name[64];           // Work queue for this goal's workers
    pid_t supervisor_pid;          // 0 if not running
    int64_t supervisor_started_at; // millis, 0 if not running
    time_t created_at;
    time_t updated_at;
} Goal;
```

**Schema:** Single `goals` table. The `queue_name` links to `work_queue` entries for this goal's workers. Actions are scoped by `goal_id` in the action store — no indirection through session_id.

**Modify:**
- `lib/services/services.h` — Add `goal_store_t* goal_store` field + accessor
- `lib/services/services.c` — Create/destroy goal_store in factory functions
- `mk/lib.mk` — Add to `LIB_DB_SOURCES`
- `mk/tests.mk` — Add test target

### Phase 2: Action Store

Separate from `task_store`. GOAP actions have preconditions/effects semantics and hierarchical parent/child relationships that don't map to the task dependency model. `task_store` remains for ralph's regular task tracking.

**New files:**
- `lib/db/action_store.h` — `Action` struct, CRUD API, readiness queries
- `lib/db/action_store.c` — Implementation using `sqlite_dal`
- `test/db/test_action_store.c` — Unit tests

**Key API:**
```c
action_store_t* action_store_create(const char* db_path);
void action_store_destroy(action_store_t* store);

// parent_action_id may be NULL for top-level actions. role may be NULL (defaults to "implementation").
int action_store_insert(action_store_t* store, const char* goal_id,
                        const char* parent_action_id, const char* description,
                        const char* preconditions_json, const char* effects_json,
                        bool is_compound, const char* role, char* out_id);

Action* action_store_get(action_store_t* store, const char* id);

int action_store_update_status(action_store_t* store, const char* id,
                               ActionStatus status, const char* result);

// Fetch all PENDING actions whose preconditions are satisfied by world_state.
// Returns both compound and primitive ready actions.
// Precondition checking is done in C after fetching all pending actions.
Action** action_store_list_ready(action_store_t* store, const char* goal_id,
                                 const char* world_state_json, size_t* count);

Action** action_store_list_by_goal(action_store_t* store, const char* goal_id,
                                    size_t* count);

// List direct children of a compound action.
Action** action_store_list_children(action_store_t* store,
                                     const char* parent_action_id, size_t* count);

int action_store_count_by_status(action_store_t* store, const char* goal_id,
                                  ActionStatus status);

// Mark all PENDING actions for a goal as SKIPPED (used when generating replacement actions).
int action_store_skip_pending(action_store_t* store, const char* goal_id);

void action_free(Action* action);
void action_free_list(Action** actions, size_t count);
```

**Modify:**
- `mk/lib.mk` — Add to `LIB_DB_SOURCES`
- `mk/tests.mk` — Add test target

### Phase 3: Goal Supervisor

The supervisor is a lightweight process that does NOT use a full `AgentSession`. It uses `llm_client_send()` directly for structured evaluation calls, keeping dependencies minimal.

**New files:**
- `lib/orchestrator/supervisor.h` — Supervisor config, lifecycle, GOAP engine API
- `lib/orchestrator/supervisor.c` — Implementation

**Supervisor GOAP loop** (in `supervisor_run()`):
```
while (running) {
    1. supervisor_collect_results()
       → poll work_queue for completed items
       → for each completed primitive action:
           → LLM call: verify effects against worker result
           → apply verified effects to world_state
           → update action status to COMPLETED
       → for each failed action:
           → increment attempt_count
           → if under max_attempts: reset to PENDING (work_queue handles retry)
           → if exhausted: mark action FAILED
       → check compound actions: if all children COMPLETED and
         effects verified in world_state → mark compound COMPLETED
       → persist updated world_state to goal_store

    2. supervisor_check_completion()
       → if world_state ⊇ goal_state → update goal to COMPLETED, exit

    3. ready = supervisor_find_ready_actions(world_state)
       → actions where status == PENDING and all preconditions satisfied

    4. for each ready action:
       → if action.is_compound:
           → LLM call: decompose into child actions
           → insert children into action_store (parent_action_id = this action)
           → update compound action status to RUNNING
       → else if worker capacity available:
           → build worker context JSON
           → select system prompt for action.role
           → work_queue_enqueue()
           → worker_spawn(queue_name, role_system_prompt)
           → update action status to RUNNING

    5. if no ready actions AND no running workers AND goal not met:
       → identify unsatisfied effects (goal_state keys not in world_state,
         or compound action effects not met by children)
       → LLM call: generate new actions to bridge the gap
       → if new actions produced: insert, loop back
       → if no viable path: update goal to FAILED, exit

    6. else if workers or decompositions pending:
       → sleep briefly, loop back to step 1
}
```

**Lean context for LLM calls** — built from scratch each time:

For compound action decomposition:
```
System: You are decomposing a project phase into concrete actions using
goal-oriented action planning. Each child action should have preconditions
and effects. Children may be compound (need further decomposition) or
primitive (directly executable by a worker agent).

Each primitive action has a role that determines what kind of worker executes it:
- "implementation": build, create, or modify code
- "code_review": review code for quality, security, and correctness
- "architecture_review": evaluate structural and design decisions
- "pm_review": verify implementation against original requirements
- "testing": write and run tests, verify behavior

Include verification actions alongside implementation actions. Verification
actions should have preconditions on the implementation effects they verify.

User:
## Goal
{goal.description}

## Phase to Decompose
{compound_action.description}

## Expected Effects
{compound_action.effects as bulleted list}

## Current World State
{world_state as key: true list}

## Completed Prerequisite Results
{results from actions whose effects are in this action's preconditions}

Return a JSON array inside a ```json fenced block:
[{"description": "...", "is_compound": bool, "role": "...", "preconditions": ["..."], "effects": ["..."]}]

Children's effects should collectively satisfy the phase's expected effects.
```

For effect verification:
```
System: You are verifying whether a completed action achieved its intended effects.

User:
## Action
{action.description}

## Worker Result
{action.result}

## Effects to Verify
{effects as bulleted list}

For each effect, respond with the effect name and YES or NO.
```

For action generation (when stuck):
```
System: You are a project planner using goal-oriented action planning.
Given the current state and desired goal, propose new actions to make progress.

User:
## Goal
{goal.description}

## Current World State
{world_state as key: true list}

## Unsatisfied Effects
{effects needed but not yet in world_state}

## Completed Actions
{completed actions with results, brief}

## Problem
No available actions can make progress toward the required effects.
Propose new actions with preconditions and effects.

Return a JSON array inside a ```json fenced block:
[{"description": "...", "is_compound": bool, "role": "...", "preconditions": ["..."], "effects": ["..."]}]
```

Each LLM call stays at ~2-4K tokens regardless of goal lifetime. Compound decomposition calls are naturally small because they focus on a single phase.

**Key design decisions:**
- Supervisor calls `llm_client_send()` + `config_get()` directly. Depends on: `llm_client.h`, `config.h`, `action_store.h`, `goal_store.h`, `workflow.h`. No dependency on `session.h`, `tools_system.h`, `approval_gate.h`, or any UI code. This minimal dependency surface is what makes the supervisor viable as a standalone HTTP-accessible process in the future.
- LLM responses parsed with `parse_api_response()` from `output_formatter.h`, which yields `ParsedResponse.response_content`. For structured output (action generation, decomposition), the supervisor extracts ` ```json ``` ` blocks and parses with cJSON.
- **Worker capacity**: `config_get_int("max_workers_per_goal", 3)` with a hard cap of 20 (matching `SUBAGENT_HARD_CAP`). Add `int max_workers_per_goal` to `agent_config_t` in `config.h`.

**Scaffold↔Supervisor communication**: The `goal_store` is the single source of truth. The supervisor updates `goal.status`, `goal.world_state`, and `goal.summary` directly in SQLite as it progresses. Scaffold polls with `goal_store_get()` — no IPC messages needed for status. Process liveness is checked with `kill(supervisor_pid, 0)`.

**Worker role specialization**: The supervisor maintains a mapping from role strings to system prompt templates. When spawning a worker for a primitive action, the supervisor selects the prompt matching `action.role` and passes it to `worker_spawn(queue_name, system_prompt)` — which already accepts a system prompt parameter. Prompt templates are loaded from a configurable directory (e.g., `~/.local/scaffold/prompts/`) following the `python_defaults/` pattern, with built-in fallbacks for the standard roles. Unknown roles get a generic worker prompt.

**Worker context building**: Before enqueuing a work item, the supervisor builds a context JSON containing:
- Goal description (1-2 sentence summary)
- This action's description and role
- Relevant world state (what's already been accomplished)
- Results from prerequisite actions (actions whose effects are in this action's preconditions)
- For verification roles: the results from the implementation actions being verified

### Phase 4: Agent Mode Integration

Wire the supervisor into the existing agent process spawning.

**Modify:**
- `lib/agent/agent.h` — Add `AGENT_MODE_SUPERVISOR = 4` to `AgentMode` enum. Add `const char* supervisor_goal_id` to `AgentConfig`.
- `lib/agent/agent.c` — Add `case AGENT_MODE_SUPERVISOR:` in `agent_run()`. Creates a `Supervisor`, calls `supervisor_run()`, cleans up. Same pattern as `AGENT_MODE_WORKER` (lines 213-297).
- `src/scaffold/main.c` — New entry point for the scaffold binary. Supports REPL mode (default) and one-shot mode (stdin/argument). Parses `--supervisor --goal <id>` for supervisor processes and `--worker --queue <name>` for workers (same as ralph).

**Modify build:**
- `mk/sources.mk` — Add `SCAFFOLD_SOURCES` alongside existing `RALPH_SOURCES`
- `Makefile` — Add scaffold binary target

### Phase 5: Scaffold Tools + Slash Commands

The orchestrator is an in-process module instantiated by the scaffold process.

**New files:**
- `lib/orchestrator/orchestrator.h` — Orchestrator lifecycle, goal management API
- `lib/orchestrator/orchestrator.c` — Implementation
- `lib/tools/orchestrator_tool.h` — Tool registration for LLM access
- `lib/tools/orchestrator_tool.c` — Tool implementations

**LLM Tools:**
| Tool | Description |
|------|-------------|
| `create_goal` | Decompose user goal into GOAP actions, create goal |
| `list_goals` | Show all goals with status and world state summary |
| `goal_status` | Detailed view: world state, action tree, progress |
| `start_goal` | Spawn supervisor for a goal |
| `pause_goal` | Signal supervisor to pause |
| `cancel_goal` | Kill supervisor and workers |

**`create_goal` implementation:**
1. Create `Goal` row in `goal_store` with status PLANNING.
2. LLM call via `llm_client_send()` with GOAP decomposition prompt: goal description → JSON output with `goal_state` (assertions) and `actions` (compound, with description + preconditions + effects) inside a fenced code block.
3. Parse response: `parse_api_response()` → extract JSON block → cJSON.
4. Store `goal_state` on the goal row. Initialize `world_state` to `{}`.
5. Create each top-level action via `action_store_insert()`. Initial actions are compound — the supervisor handles further decomposition.
6. Update goal status to ACTIVE.
7. Return goal ID, action count, and goal state summary via `tool_result_builder`.

The tool follows existing patterns: global `Services*` pointer set via explicit `*_set_services()` wiring (see `memory_tool.c`), parameters extracted with `extract_string_param()`, results via `tool_result_builder_create/set_success/finalize`.

**Slash Commands** (direct state inspection, bypass LLM):
| Command | Description |
|---------|-------------|
| `/goals` | List all goals with status |
| `/tasks` | Show action tree for current/specified goal |
| `/agents` | Show running supervisors and workers |
| `/memory` | Access semantic memory (existing) |

`/tasks` displays the action hierarchy as a tree — compound actions as parent nodes, children indented beneath. Status, effects, and worker results shown per action. This gives the user a natural view of project phases and their progress.

Slash commands read directly from stores and format output for the terminal without an LLM round-trip.

**Modify:**
- `lib/agent/session.c` — Init orchestrator during `session_init()` when running as scaffold, register tools
- `mk/lib.mk` — Add `LIB_ORCHESTRATOR_SOURCES`

### Phase 6: Worker Result Capture

Workers currently report "Task completed successfully" as the result. Workers need to capture the LLM's final response so supervisors can verify effects against actual output.

**Modify:**
- `lib/agent/agent.c` (worker loop, line 273) — After `session_process_message()`, extract the last assistant message from `conversation.data[conversation.count-1].content` and pass it to `work_queue_complete()` as the result instead of the static string.

### Phase 7: Recovery

Handle crashes and restarts.

- **Stale supervisor detection**: On scaffold init, scan `goal_store` for goals with `supervisor_pid != 0`. Check each PID with `kill(pid, 0)`. If the process is gone (errno == ESRCH), clear the PID and set status to PAUSED for user-initiated restart. Use `supervisor_started_at` to guard against PID recycling — treat any supervisor older than 1 hour with a dead PID as definitively stale.
- **Zombie reaping**: Scaffold calls `waitpid(pid, &status, WNOHANG)` for each tracked supervisor during its polling cycle, following `subagent_poll_all()` patterns. On `cancel_goal`: `kill(pid, SIGTERM)` → `usleep(100ms)` → `kill(pid, SIGKILL)` → `waitpid(pid, &status, 0)`.
- **Action retry**: The `work_queue` handles retry mechanics (failed items retry up to `max_attempts`). The supervisor also tracks `attempt_count` on actions for its own retry decisions.
- **Goal resume**: `start_goal` on an existing goal picks up from current world state and action status. The GOAP loop naturally handles partial progress — compound actions already decomposed keep their children, primitives already completed keep their effects in world state.
- **World state consistency**: If a supervisor crashes mid-update, world state might be stale (effects verified but not persisted). On restart, the supervisor re-scans completed actions and re-verifies any whose effects aren't reflected in world state.

## File Summary

| Action | File | Purpose |
|--------|------|---------|
| Create | `lib/db/goal_store.h` | Goal struct + CRUD API |
| Create | `lib/db/goal_store.c` | SQLite-backed implementation |
| Create | `lib/db/action_store.h` | Action struct + hierarchy + readiness queries |
| Create | `lib/db/action_store.c` | SQLite-backed implementation |
| Create | `lib/orchestrator/supervisor.h` | GOAP/HTN engine API |
| Create | `lib/orchestrator/supervisor.c` | Supervisor loop + LLM evaluation |
| Create | `lib/orchestrator/orchestrator.h` | Multi-goal management API |
| Create | `lib/orchestrator/orchestrator.c` | Supervisor spawning + lifecycle |
| Create | `lib/tools/orchestrator_tool.h` | LLM tool registration |
| Create | `lib/tools/orchestrator_tool.c` | Tool implementations |
| Create | `src/scaffold/main.c` | Scaffold binary entry point |
| Create | `test/db/test_goal_store.c` | Goal store unit tests |
| Create | `test/db/test_action_store.c` | Action store + hierarchy tests |
| Create | `test/orchestrator/test_supervisor.c` | Supervisor GOAP/HTN decision tests |
| Modify | `lib/services/services.h` | Add `goal_store`, `action_store` fields |
| Modify | `lib/services/services.c` | Wire stores in factory functions |
| Modify | `lib/agent/agent.h` | Add `AGENT_MODE_SUPERVISOR` |
| Modify | `lib/agent/agent.c` | Add supervisor case + worker result capture |
| Modify | `lib/util/config.h` | Add `max_workers_per_goal` field |
| Modify | `lib/util/config.c` | Default, JSON load/save |
| Modify | `mk/lib.mk` | Add `LIB_ORCHESTRATOR_SOURCES`, `LIB_DB_SOURCES` additions |
| Modify | `mk/sources.mk` | Add `SCAFFOLD_SOURCES` |
| Modify | `mk/tests.mk` | Add test targets |
| Modify | `Makefile` | Add scaffold binary target |

## Key Reuse

These existing primitives are used **as-is** with no modification:
- `work_queue` / `worker_spawn()` — Work distribution, claiming, retry
- `sqlite_dal` — Database access patterns (config, binders, mappers, transactions)
- `parse_api_response()` — Extract `ParsedResponse.response_content` from raw LLM JSON
- `llm_client_send()` — Supervisor's LLM calls for decomposition, verification, and generation
- `config_get()` / `config_get_int()` — API credentials and `max_workers_per_goal`
- `tool_result_builder` — Consistent tool result formatting for orchestrator tools

Note: `task_store` is **not** reused for GOAP actions. `task_store` remains available for ralph's regular task tracking via the todo tools.

## Verification

1. **Goal store**: Build and run `test/test_goal_store` — CRUD, status transitions, world state persistence
2. **Action store**: Build and run `test/test_action_store` — CRUD, hierarchy (parent/child), readiness queries with world state, precondition matching
3. **Supervisor GOAP/HTN**: Build and run `test/test_supervisor` — mock LLM responses, verify: compound decomposition produces children, primitive dispatch to workers, effect verification updates world state, goal completes when world state satisfies goal state, stuck state generates new actions
4. **Integration**: Create a goal via scaffold REPL, verify supervisor decomposes compound actions into children, workers execute primitives, effects bubble up through hierarchy, goal completes
5. **Recovery**: Kill a supervisor, restart scaffold, verify stale PID detection and resume from current world state with hierarchy intact
6. **Memory safety**: `make check-valgrind` on all new test binaries (exclude subagent/fork tests per existing policy)
