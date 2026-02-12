# Scaffold: Goal-Oriented Orchestration

## Context

Scaffold is an orchestrator built on ralph. Two binaries, one library:

- **libagent** (`libagent.a`): Generic agent library — tool calling, sub-agents, messaging, memory, MCP, LLM providers.
- **ralph** (`src/ralph/main.c`): A standalone AI agent binary with semantic memory, Python tooling, MCP support, task tracking.
- **scaffold** (`src/scaffold/main.c`): An orchestrator binary that coordinates goal decomposition, supervision, and worker execution.

It's ralphs all the way down.

Both binaries link against `libagent.a`. `ralph` is the standalone agent. `scaffold` is the orchestrator. Every process in scaffold is a full agent with all tooling — the difference is the system prompt and the task.

**Core insight**: Every stage gets its own agent with its own context window. Work (goals, actions, results, world state) is persistent and lives in SQLite stores. Agents are organized into layers — planning, supervision, execution — each with a focused scope that prevents context blowup by architecture, not by relying on compaction. No agent ever sees another agent's conversation history; results flow through the stores.

**Planning model**: Combines Goal-Oriented Action Planning (GOAP) with Hierarchical Task Network (HTN) decomposition. Goals are desired world states. Actions are transforms with preconditions and effects — dependencies emerge mechanically from precondition/effect chains. Actions can be **compound** (decomposed into children just-in-time by the supervisor) or **primitive** (dispatched to workers). This hierarchical decomposition means the LLM only needs to plan one level of detail at a time, with full context from completed prerequisite work.

## Architecture

Three agent roles, two context boundaries. Both `ralph` and `scaffold` are thin CLI wrappers around `libagent.a` — they share the same agent lifecycle (`agent_init` → `agent_run` → `agent_cleanup`) and differ only in `app_name` and app-specific extensions. `worker_spawn()` uses `get_executable_path()` to fork the current binary, so all child processes are scaffold instances.

```
scaffold (interactive REPL or one-shot mode)
  └── Plan finalized → context cleared → decompose into goals
        ├── Goal A → Supervisor agent (fresh context)
        │              ├── Worker (action 1) → dies
        │              ├── Worker (action 2) → dies
        │              └── Worker (action 3) → dies
        └── Goal B → Supervisor agent (fresh context)
                       └── Worker (action 4) → dies
```

- **Scaffold agent**: The user-facing process. In REPL mode, the user collaborates with the agent to build a plan. In one-shot mode (`cat spec.md | ./scaffold` or `./scaffold "build X"`), the plan arrives via stdin/argument. Once the plan is finalized, the scaffold clears its context (plan-mode → execution-mode handoff) and decomposes the plan into goals with GOAP structure — goal state assertions, world state, initial compound actions — using GOAP tools (`goap_create_goal`, `goap_create_actions`). This is one short LLM interaction on a fresh context. The scaffold then spawns one supervisor per goal and monitors progress.
- **Goal Supervisor agents**: One per active goal, fresh context. Ensures the goal reaches completion by managing the GOAP loop via tool calls: find ready actions → decompose compound ones → dispatch primitives to workers → verify effects from completed work → replan when stuck. Reads worker results from the store, never from worker conversations. Context stays bounded because each interaction is tool-call-heavy with bounded store reads. Event-driven — sleeps between worker dispatches, wakes on worker completion messages via the message poller (see Supervisor Event Loop below).
- **Worker agents**: Ephemeral, fresh context per action. Receive an action description + prerequisite results as their task. Execute using the full toolset with a role-specific system prompt (implementation, code review, testing, etc.). Report result. Die when queue is empty. The current `AGENT_MODE_WORKER` sleeps indefinitely on empty queue — this needs modification to exit cleanly instead (see Phase 5).

All agents are full scaffold processes with the complete toolkit — GOAP tools, file editing, shell, everything. Context blowup is prevented by architecture: the scaffold clears context before decomposition, supervisors never see worker conversations (results live in the store), workers are ephemeral. Only the scaffold agent might accumulate context from user conversation (pre-plan).

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

World state is strictly an **ordering and completion lattice** — it answers "what conditions hold?" for mechanical readiness checks and goal completion tests. It is not a knowledge base. When the supervisor or a downstream action needs to know *what* was built (schema details, API shape, implementation choices), it reads **action results** from the store via `goap_get_action_results`. This separation is critical: if world state carried semantic values (strings, JSON objects), precondition checking would require an LLM call ("does this state satisfy this precondition?"), defeating GOAP's core property that ordering is *derived mechanically*, not *decided*.

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

Each primitive action carries a `role` that determines the worker's system prompt. The supervisor maps roles to system prompt templates when spawning workers. Note: `worker_spawn()` accepts a `system_prompt` parameter in its signature but currently ignores it (`(void)system_prompt` with a "future: --system-prompt flag" comment). Phase 5 must wire this through — writing the prompt to a temp file and passing the path via CLI flag (avoids `execv` argument length limits).

| Role | System Prompt Focus |
|------|-------------------|
| `implementation` | Build, create, modify code. Default role for workers. |
| `code_review` | Review code for quality, security, error handling, style. Read files, check patterns, report issues. |
| `architecture_review` | Evaluate structural decisions, module boundaries, dependency patterns. |
| `design_review` | Assess UX/UI decisions, API surface design, data model choices. |
| `pm_review` | Check implementation against original requirements. Verify acceptance criteria are met. |
| `testing` | Write and run tests, verify behavior, check edge cases. |

Roles are strings, not an enum — new roles can be added without code changes. The supervisor loads prompt templates from a configurable directory (e.g., `~/.local/scaffold/prompts/`), following the runtime file-loading pattern used by `prompt_loader.c` (loads `AGENTS.md` from CWD) and `config.c` (loads JSON from `app_home_get()` directory). Falls back to built-in default prompts for the standard roles and a generic worker prompt for unknown roles.

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

1. **Initial decomposition** (scaffold agent, post-context-clear): The plan is decomposed into goals, each with goal state assertions and initial compound actions (high-level phases). This is a coarse plan — 3-5 actions per goal, not 30. The scaffold agent does this directly after clearing its context, using GOAP tools in one short LLM interaction.
2. **Just-in-time decomposition** (supervisor agent): When a compound action becomes ready (preconditions satisfied), the supervisor decomposes it into children via GOAP tools. Children can be primitive (worker-executable) or further compound (decomposed later). The supervisor has results from prerequisite actions available in the store, so it can plan with more context than the initial decomposition had.
3. **Primitive dispatch**: When a primitive action becomes ready, the supervisor dispatches it to a worker via GOAP tools.
4. **Continuous replanning**: If the supervisor is stuck at any level (no ready actions, no running workers, effects not met), it generates new actions bridging the gap. This works identically for top-level stuck states and for compound actions whose children didn't fully satisfy the contract.

### How Each Layer Uses the LLM

Every layer goes through `session_process_message()` — no raw `llm_client_send()` calls. Each agent's LLM drives its work through tool calls:

- **Scaffold agent**: Two phases. (1) **Planning**: conversational — the LLM helps the user refine a plan via normal conversation. Once finalized, the LLM calls `execute_plan` which clears the context. (2) **Decomposition + monitoring**: on the fresh context, the LLM decomposes the plan into goals and initial compound actions via GOAP tools (`goap_create_goal`, `goap_create_actions`), spawns supervisors, and enters monitoring mode. One short decomposition interaction, then the scaffold reports progress to the user as supervisor status messages arrive.
- **Supervisor agent**: Receives a goal ID as its task. The LLM examines the state via GOAP tools (`goap_get_goal`, `goap_list_actions`, `goap_get_action_results`) and progresses the goal via tool calls (`goap_create_actions`, `goap_dispatch_action`, `goap_update_world_state`, `goap_check_complete`). The supervisor is event-driven — after dispatching workers, it sleeps until worker completion messages arrive via the message poller (see Supervisor Event Loop). If it dies (context full, crash), the scaffold agent respawns it — the new instance reads current state from the stores and continues. Context stays naturally bounded because worker results come from the store (not from conversation history) and each supervisor manages a single focused goal.
- **Worker agent**: Receives action description + prerequisite results as its task. Uses the full toolset (file editing, shell, etc.) to complete the action. Reports result via work queue. Dies.

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

### Phase 3: Worker Result Capture

Workers currently report "Task completed successfully" as the result. Workers need to capture the LLM's final response so supervisors can verify effects against actual output. This must be done before GOAP tools and supervisors are built — supervisors read worker results from the store to verify effects and plan next steps.

**Modify:**
- `lib/agent/agent.c` (worker loop, `AGENT_MODE_WORKER` case, inside the `result == 0` branch where `work_queue_complete()` is called with the static `"Task completed successfully"` string) — After `session_process_message()`, extract the last assistant message from `agent->session.session_data.conversation` (a `ConversationHistory` DARRAY with `.data[]` and `.count`) and pass its `.content` to `work_queue_complete()` as the result instead of the static string. The conversation history is valid here because `session_process_message()` appends the assistant response before returning.

### Phase 4: GOAP Tools + Goal Supervisor

The supervisor is a full agent — it goes through `session_process_message()` like every other layer. The GOAP logic lives in tools that the supervisor's LLM calls, not in hand-crafted prompt/response cycles.

**New files:**
- `lib/tools/goap_tools.h` — GOAP tool registration + `goap_tools_set_services()`
- `lib/tools/goap_tools.c` — Tool implementations
- `lib/orchestrator/supervisor.h` — Supervisor session loop
- `lib/orchestrator/supervisor.c` — Implementation
- `test/orchestrator/test_goap_tools.c` — GOAP tool unit tests

**GOAP Tools** (registered for all scaffold agents, used primarily by supervisors and the scaffold agent during decomposition):

| Tool | Description |
|------|-------------|
| `goap_get_goal` | Read goal description, goal state, world state |
| `goap_list_actions` | List actions for a goal, filtered by status/parent |
| `goap_create_goal` | Create a new goal with goal state assertions |
| `goap_create_actions` | Insert actions (compound or primitive) with preconditions/effects |
| `goap_update_action` | Update action status/result |
| `goap_dispatch_action` | Enqueue primitive action to work queue + spawn worker |
| `goap_update_world_state` | Set world state assertions (verified effects) |
| `goap_check_complete` | Test if `world_state ⊇ goal_state` |
| `goap_get_action_results` | Read completed action results from store |

Tools follow existing patterns: global `Services*` pointer via `goap_tools_set_services()`, parameters via `extract_string_param()`, results via `tool_result_builder`. The `goap_dispatch_action` tool handles worker context building (goal summary, action description, prerequisite results from store) and role-based system prompt selection internally. It also enforces the worker capacity cap — before spawning, it counts running workers for the goal and returns an error if at `max_workers_per_goal`. Workers are spawned as subagents of the supervisor, so completion triggers `subagent_notify_parent()` automatically (see Supervisor Event Loop).

**Supervisor event loop:**

The supervisor runs as `AGENT_MODE_SUPERVISOR` — a stripped-down REPL without stdin. The lifecycle:

1. **Initial kick**: `supervisor_run()` reads goal state from stores, builds a status summary, calls `session_process_message()`. The LLM examines the state and calls GOAP tools — decompose compound actions, dispatch primitives, verify effects.
2. **Wait for events**: After the LLM's turn completes (workers dispatched, nothing left to do until they finish), the supervisor enters an event loop: `select()` on the message poller fd only (no stdin). This is the REPL event loop minus readline.
3. **Worker completion notification**: Workers are spawned as subagents. When a worker completes, `subagent_notify_parent()` sends a direct message via `message_send_direct()`. The supervisor's message poller detects it (within `poll_interval_ms`), writes `'M'` to the notification pipe, and `select()` unblocks.
4. **Out-of-band LLM call**: The event loop fetches the message, injects it as a system notification into the conversation, and calls `session_continue()`. The LLM sees "worker X completed with result Y" and responds with GOAP tool calls — verify effects, update world state, dispatch next actions.
5. **Repeat** steps 2-4 until `world_state ⊇ goal_state` → supervisor exits cleanly.
6. **Context full**: If the supervisor's context fills up, it exits. The scaffold agent detects the dead PID and respawns.

The supervisor reads worker results from the action store / work queue (structured data for GOAP tool calls), not from the notification message itself. The notification is just the wake-up trigger.

**Crash resilience:**

All goal state is external in the stores. Supervisors are designed to die and restart:

- If context fills up, the supervisor exits
- If the process crashes, the scaffold agent detects the dead PID
- In both cases, the scaffold agent respawns the supervisor for the same goal
- The new instance reads current world state, action statuses, and worker results from the stores and continues where the last one left off
- The GOAP model naturally supports this — no in-memory state to lose

This is the same principle as workers (ephemeral, die after work) applied to supervision: agents are like processes in an actor framework, designed to die and restart around persistent external state. Each supervisor is told "ensure this goal is complete" and keeps getting respawned until it can report that `world_state ⊇ goal_state`.

**Key design decisions:**
- **Worker capacity**: `config_get_int("max_workers_per_goal", 3)` with a hard cap of 20 (matching `SUBAGENT_HARD_CAP`). Add `int max_workers_per_goal` to `agent_config_t` in `config.h`.
- **Scaffold↔Supervisor communication**: The `goal_store` is the single source of truth. The supervisor updates `goal.status`, `goal.world_state`, and `goal.summary` directly in SQLite as it progresses. Scaffold polls with `goal_store_get()` — no IPC messages needed for status. Process liveness is checked with `kill(supervisor_pid, 0)`.
- **Worker role specialization**: The `goap_dispatch_action` tool selects a system prompt matching `action.role` and passes it to `worker_spawn(queue_name, system_prompt)`. Prompt templates are loaded from a configurable directory (e.g., `~/.local/scaffold/prompts/`), following the runtime file-loading patterns in `prompt_loader.c` and `config.c`, with built-in fallbacks for the standard roles. Unknown roles get a generic worker prompt.
- **Worker context building**: The `goap_dispatch_action` tool builds the work item context from the stores: goal description (1-2 sentence summary), action description and role, relevant world state, results from prerequisite actions, and for verification roles, the results from the implementation actions being verified.

### Phase 5: Agent Mode Integration

Wire the supervisor into the existing agent process spawning.

**Modify:**
- `lib/agent/agent.h` — Add `AGENT_MODE_SUPERVISOR = 4` to `AgentMode` enum (after existing `AGENT_MODE_WORKER = 3`). Add `const char* supervisor_goal_id` to `AgentConfig` (alongside existing `worker_queue_name` and `worker_system_prompt` fields).
- `lib/agent/agent.c` — Add `case AGENT_MODE_SUPERVISOR:` in `agent_run()`. Creates a `Supervisor`, calls `supervisor_run()`, cleans up. Same pattern as the `AGENT_MODE_WORKER` case (signal handler setup, create resource, run loop, destroy resource on exit). Also modify `AGENT_MODE_WORKER`: when `work_queue_claim()` returns NULL, exit cleanly instead of sleeping (`usleep(1000000)` → `break`), so workers die after draining their queue.
- `lib/workflow/workflow.c` — Wire `system_prompt` through `worker_spawn()`: write the prompt to a temp file and pass the path via `--system-prompt-file <path>` CLI flag to the child process. Temp file avoids OS-level `execv` argument length limits for long prompts. Currently `system_prompt` is accepted but unused (`(void)system_prompt` at line 409).
- `src/scaffold/main.c` — Already exists with REPL, single-shot, worker, and subagent modes. Add `--supervisor --goal <id>` argument parsing (same pattern as existing `--worker --queue <name>`).

**Modify build:**
- `mk/sources.mk` — `SCAFFOLD_SOURCES` already exists (pointing to `src/scaffold/main.c`), no change needed
- `Makefile` — Add scaffold binary target (if not already present)

### Phase 6: Scaffold Orchestrator Tools + Slash Commands

The scaffold agent needs tools to manage the overall lifecycle — executing plans (with inline decomposition), spawning supervisors, monitoring progress. These are separate from the GOAP tools (Phase 4) which all agents share.

**New files:**
- `lib/orchestrator/orchestrator.h` — Orchestrator lifecycle, supervisor spawning/monitoring
- `lib/orchestrator/orchestrator.c` — Implementation
- `lib/tools/orchestrator_tool.h` — Tool registration for scaffold agent
- `lib/tools/orchestrator_tool.c` — Tool implementations

**Scaffold Agent Tools** (user-facing goal lifecycle):

| Tool | Description |
|------|-------------|
| `execute_plan` | Clear context, decompose plan into goals, spawn supervisors |
| `list_goals` | Show all goals with status and world state summary |
| `goal_status` | Detailed view: world state, action tree, progress |
| `pause_goal` | Signal supervisor to pause |
| `cancel_goal` | Kill supervisor and workers |

**`execute_plan` implementation:**
1. Clear the scaffold agent's conversation context (plan-mode → execution-mode handoff).
2. Return a tool result containing the plan text and a decomposition instruction. On the fresh context, the scaffold agent's LLM sees the plan and decomposes it into goals and initial compound actions using GOAP tools (`goap_create_goal`, `goap_create_actions`).
3. The scaffold agent spawns one supervisor per goal (via orchestrator functions) and enters monitoring mode.

No separate decomposer process — the scaffold agent handles decomposition directly on its fresh post-clear context. This is one short LLM interaction (plan in, goals + actions out via tool calls).

Tools follow existing patterns: global `Services*` pointer via `orchestrator_tool_set_services()` (see `memory_tool.c`), parameters via `extract_string_param()`, results via `tool_result_builder`.

**Slash Commands** (direct state inspection, bypass LLM):
| Command | Description |
|---------|-------------|
| `/goals` | List all goals with status |
| `/tasks` | Show action tree for current/specified goal |
| `/agents` | Show running supervisors and workers |
| `/memory` | Access semantic memory (existing) |

`/tasks` displays the action hierarchy as a tree — compound actions as parent nodes, children indented beneath. Status, effects, and worker results shown per action.

Slash commands read directly from stores and format output for the terminal without an LLM round-trip.

**Modify:**
- `lib/agent/session.c` — Register GOAP tools and orchestrator tools during `session_init()` when running as scaffold (check `app_name`). Wire services via `goap_tools_set_services()` and `orchestrator_tool_set_services()` in `session_wire_services()` (same pattern as `memory_tool_set_services()`, `vector_db_tool_set_services()`, etc.)
- `lib/tools/builtin_tools.c` — Add `register_goap_tools()` and `register_orchestrator_tools()` calls (conditioned on scaffold mode) alongside existing `register_memory_tools()`, `register_vector_db_tools()`, etc.
- `mk/lib.mk` — Add `LIB_ORCHESTRATOR_SOURCES` to the library module list

### Phase 7: Recovery + Supervisor Respawn

Agents are designed to die and restart. All state is external in the stores — no in-memory state to lose.

- **Automatic supervisor respawn**: The scaffold agent monitors supervisor PIDs. When a supervisor dies (context full, crash, or clean exit without goal completion), the scaffold agent respawns it for the same goal. The new instance reads current world state, action statuses, and worker results from the stores and continues where the last one left off. The GOAP model naturally supports this — a freshly spawned supervisor examining the stores is indistinguishable from one that's been running.
- **Stale supervisor detection**: On scaffold init, scan `goal_store` for goals with `supervisor_pid != 0`. Check each PID with `kill(pid, 0)`. If the process is gone (errno == ESRCH), clear the PID and respawn. Use `supervisor_started_at` to guard against PID recycling — treat any supervisor older than 1 hour with a dead PID as definitively stale.
- **Zombie reaping**: Scaffold calls `waitpid(pid, &status, WNOHANG)` for each tracked supervisor during its polling cycle, following the pattern in `subagent_poll_all()` (`lib/tools/subagent_tool.c`). On `cancel_goal`: `kill(pid, SIGTERM)` → `usleep(100ms)` → `kill(pid, SIGKILL)` → `waitpid(pid, &status, 0)` (same sequence as `worker_stop()` in `workflow.c`).
- **Action retry**: The `work_queue` handles retry mechanics (failed items retry up to `max_attempts`). The supervisor also tracks `attempt_count` on actions for its own retry decisions.
- **Goal resume**: Any supervisor spawn (initial or respawn) picks up from current world state and action status. The GOAP tools return current state from the stores. Compound actions already decomposed keep their children, primitives already completed keep their effects in world state. There is no distinction between "resume" and "start" — both are just "ensure this goal is complete."
- **World state consistency**: If a supervisor dies mid-update, world state might be stale (effects verified but not persisted). On respawn, the supervisor's LLM sees completed actions whose effects aren't reflected in world state and re-verifies them naturally through GOAP tool calls.

## File Summary

| Action | File | Purpose |
|--------|------|---------|
| Create | `lib/db/goal_store.h` | Goal struct + CRUD API |
| Create | `lib/db/goal_store.c` | SQLite-backed implementation |
| Create | `lib/db/action_store.h` | Action struct + hierarchy + readiness queries |
| Create | `lib/db/action_store.c` | SQLite-backed implementation |
| Create | `lib/tools/goap_tools.h` | GOAP tool registration (shared by all scaffold agents) |
| Create | `lib/tools/goap_tools.c` | GOAP tool implementations |
| Create | `lib/orchestrator/supervisor.h` | Supervisor session loop + respawn support |
| Create | `lib/orchestrator/supervisor.c` | Implementation |
| Create | `lib/orchestrator/orchestrator.h` | Goal lifecycle management (scaffold agent layer) |
| Create | `lib/orchestrator/orchestrator.c` | Supervisor spawning, monitoring, respawn |
| Create | `lib/tools/orchestrator_tool.h` | Scaffold agent tool registration |
| Create | `lib/tools/orchestrator_tool.c` | Scaffold agent tool implementations |
| Modify | `src/scaffold/main.c` | Add `--supervisor --goal` argument parsing (binary already exists) |
| Create | `test/db/test_goal_store.c` | Goal store unit tests |
| Create | `test/db/test_action_store.c` | Action store + hierarchy tests |
| Create | `test/tools/test_goap_tools.c` | GOAP tool unit tests |
| Create | `test/orchestrator/test_supervisor.c` | Supervisor respawn + goal completion tests |
| Modify | `lib/services/services.h` | Add `goal_store_t*`, `action_store_t*` fields + accessors (alongside existing `task_store_t*`, `document_store_t*`, etc.) |
| Modify | `lib/services/services.c` | Create/destroy in `services_create_default()` / `services_destroy()` |
| Modify | `lib/agent/agent.h` | Add `AGENT_MODE_SUPERVISOR` |
| Modify | `lib/agent/agent.c` | Worker result capture (Phase 3), supervisor case + worker exit-on-empty (Phase 5) |
| Modify | `lib/workflow/workflow.c` | Wire `system_prompt` through `worker_spawn()` |
| Modify | `lib/tools/builtin_tools.c` | Register GOAP + orchestrator tools (scaffold mode) |
| Modify | `lib/util/config.h` | Add `int max_workers_per_goal` field to `agent_config_t` |
| Modify | `lib/util/config.c` | Default value, JSON load/save, add key mapping in `config_get_int()` |
| Modify | `mk/lib.mk` | Add `LIB_ORCHESTRATOR_SOURCES`, `LIB_DB_SOURCES` additions |
| Modify | `mk/sources.mk` | `SCAFFOLD_SOURCES` already exists — no change needed |
| Modify | `mk/tests.mk` | Add test targets |
| Modify | `Makefile` | Add scaffold binary target |

## Key Reuse

These existing primitives are used **as-is** with no modification:
- `session_process_message()` — Every layer (scaffold, supervisor, worker) drives its work through this. No raw `llm_client_send()` calls anywhere.
- `session_continue()` — Out-of-band LLM call triggered by message poller notifications. Used by supervisor event loop to respond to worker completion.
- `message_poller` / `message_send_direct()` / `subagent_notify_parent()` — Event-driven supervisor wake-up when workers complete.
- `work_queue` — Work distribution (`work_queue_enqueue`, `work_queue_claim`), completion (`work_queue_complete`), retry (`work_queue_fail` requeues if `attempt_count < max_attempts`)
- `worker_is_running()` / `worker_stop()` — Worker process liveness checks and forced termination (for `cancel_goal`)
- `sqlite_dal` — Database access patterns (config struct, `BindText*`/`BindInt64` binders, `sqlite_row_mapper_t` mappers, `sqlite_dal_query_list_p`/`sqlite_dal_exec_p` parameterized queries, transactions)
- `config_get()` / `config_get_int()` — API credentials and `max_workers_per_goal`
- `tool_result_builder` — Consistent tool result formatting (`tool_result_builder_create` → `set_success`/`set_error` → `finalize`)
- `extract_string_param()` — JSON parameter extraction for tool arguments (from `common_utils.h`)

These primitives require **modification** (detailed in phases above):
- `worker_spawn()` — System prompt parameter must be wired through to child process via temp file (currently unused: `(void)system_prompt`) — Phase 5
- `AGENT_MODE_WORKER` — Two changes: (1) capture actual LLM response from `session_data.conversation` instead of static `"Task completed successfully"` string — Phase 3; (2) exit when queue is empty instead of sleeping forever (`usleep` loop → clean exit), so workers die naturally after draining their queue — Phase 5

Note: `task_store` is **not** reused for GOAP actions. GOAP actions have precondition/effect semantics and hierarchical compound/primitive relationships that don't map to task_store's dependency model. `task_store` remains available for ralph's regular task tracking via the todo tools.

## Verification

1. **Goal store**: Build and run `test/test_goal_store` — CRUD, status transitions, world state persistence
2. **Action store**: Build and run `test/test_action_store` — CRUD, hierarchy (parent/child), readiness queries with world state, precondition matching
3. **Worker result capture**: Verify worker passes LLM's final response to `work_queue_complete()` instead of static string
4. **GOAP tools**: Build and run `test/test_goap_tools` — mock stores, verify each tool returns correct results, dispatch enforces worker cap, dispatch creates work items + spawns workers
5. **Supervisor lifecycle**: Build and run `test/test_supervisor` — verify supervisor reads state from stores on startup, progresses goal via tool calls, sleeps on message poller, wakes on worker completion, exits cleanly when goal complete
6. **Respawn**: Kill a supervisor mid-goal, respawn it, verify it reads current state from stores and continues — no distinction between fresh start and respawn
7. **Integration**: Create a plan via scaffold REPL, verify scaffold decomposes into goals, supervisors progress them via GOAP tools, workers execute primitives, effects bubble up, goals complete
8. **Memory safety**: `make check-valgrind` on all new test binaries (exclude subagent/fork tests per existing policy)
