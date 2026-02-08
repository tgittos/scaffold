# Orchestrator System: Actor-Based Project Management

## Context

Ralph's agent layer is being extracted into `libralph.a` with clean lifecycle APIs (`agent_init` / `agent_run` / `agent_cleanup`). The codebase already has the primitives for multi-agent coordination: work queues, task store with DAG dependencies, IPC messaging, and worker processes. The next step is an **orchestrator** that manages long-lived projects using ephemeral workers.

**Core insight**: Flip the worker/work relationship. Work (projects, tasks, results) is persistent and lives in SQLite stores. Workers are ephemeral processes that pick up a task, execute it with a fresh context window, report results, and die. Project supervisors maintain lean context (goal + task status table) and never accumulate worker conversation histories.

## Architecture

```
Interactive Agent (user-facing REPL)
  └── Orchestrator (in-process, manages projects)
        ├── Project A ──→ Supervisor process A
        │                   ├── Worker (task 1) → dies
        │                   ├── Worker (task 2) → dies
        │                   └── Worker (task 3) → dies
        └── Project B ──→ Supervisor process B
                            └── Worker (task 4) → dies
```

- **Orchestrator**: Lives inside the interactive agent session. Exposes LLM tools (`create_project`, `project_status`, etc.) so the user manages projects through conversation.
- **Supervisor**: One forked process per active project. Runs a decide/act loop: read task DAG → spawn workers for ready tasks → collect results → evaluate with a lean LLM call → repeat.
- **Worker**: Ephemeral process (`AGENT_MODE_WORKER`). Claims a work item, runs `session_process_message()` with fresh conversation history, reports the result string, exits.

## Implementation Phases

### Phase 1: Project Store

New SQLite-backed store following `task_store` / `sqlite_dal` patterns.

**New files:**
- `lib/db/project_store.h` — `Project` struct, CRUD API
- `lib/db/project_store.c` — Implementation using `sqlite_dal`
- `test/db/test_project_store.c` — Unit tests

**Project struct:**
```c
typedef struct {
    char id[40];                  // UUID
    char name[128];               // Human-readable
    char* goal;                   // Full project description
    char* summary;                // Rolling status summary (supervisor updates)
    ProjectStatus status;         // ACTIVE, PAUSED, COMPLETED, FAILED
    char session_id[40];          // Scopes task_store entries for this project
    char queue_name[64];          // Work queue name for this project's workers
    pid_t supervisor_pid;         // 0 if not running
    int64_t supervisor_started_at; // millis, 0 if not running (stale PID detection)
    time_t created_at;
    time_t updated_at;
} Project;
```

**Schema:** Single `projects` table. The `session_id` links to `task_store` entries — all tasks for a project share one `session_id`, reusing the existing task_store session scoping.

**Modify:**
- `lib/services/services.h` — Add `project_store_t* project_store` field + accessor
- `lib/services/services.c` — Create/destroy project_store in factory functions
- `mk/lib.mk` — Add to `LIB_DB_SOURCES`
- `mk/tests.mk` — Add test target

### Phase 2: Supervisor

The supervisor is a **lightweight process** that does NOT use a full `AgentSession`. It uses `llm_client_send()` directly for structured evaluation calls, keeping dependencies minimal.

**New files:**
- `lib/orchestrator/supervisor.h` — Supervisor config, lifecycle, decision engine API
- `lib/orchestrator/supervisor.c` — Implementation

**Supervisor loop** (in `supervisor_run()`):
```
while (running) {
    1. supervisor_collect_results()    // poll work_queue for completed items
       → update task_store status for completed/failed tasks
       → update project summary

    2. action = supervisor_decide()    // examine task DAG state:
       ready_tasks  = task_store_list_ready(session_id)  // pending + unblocked
       active       = count of live worker processes
       all_tasks    = task_store_list_by_session(session_id)
       pending      = count where status == PENDING (includes blocked)
       completed    = count where status == COMPLETED

       → all tasks COMPLETED                           → COMPLETE
       → ready tasks exist AND worker capacity          → SPAWN_WORKER
       → active workers > 0 (will unblock dependents)  → WAIT
       → pending > 0 AND active == 0 (deadlock)        → REPLAN
       → repeated failures on same task                 → REPLAN
       → else                                           → FAIL

    3. supervisor_execute_action(action)
       → SPAWN_WORKER: build context JSON, work_queue_enqueue(), worker_spawn()
       → WAIT: sleep briefly, poll workers, loop back to step 1
       → COMPLETE: update project status → COMPLETED, exit
       → REPLAN: LLM call to revise task list, task_store_replace_session_tasks()
       → FAIL: update project status → FAILED, exit
}
```

**WAIT explained**: This only triggers when workers are actively running. Their completion will mark tasks as COMPLETED in the task_store, which unblocks dependent tasks (removing them from `blocked_by`). On the next loop iteration, `task_store_list_ready()` returns the newly unblocked tasks. If all workers die without unblocking anything AND pending tasks remain, that's a deadlock — the supervisor detects this (pending > 0, active == 0) and escalates to REPLAN.

**Lean context for LLM calls** — The supervisor builds a prompt from scratch each time:
```
System: You are a project supervisor. Given the project goal and task status,
decide if the project is complete, needs replanning, or should continue.

User:
## Goal
{project.goal}

## Tasks
| ID | Description | Status | Result |
| ...rows from task_store... |

## Question
[Are we done? / What new tasks are needed? / Should we retry task X?]
```

This keeps the supervisor at ~2K-4K tokens regardless of project lifetime.

**Key design decision**: The supervisor calls `llm_client_send()` + `config_get()` directly rather than going through the agent layer. It depends on: `llm_client.h`, `config.h`, `task_store.h`, `workflow.h`, `project_store.h`. No dependency on `session.h`, `tools_system.h`, `approval_gate.h`, or any UI code.

**LLM call mechanics**: `llm_client_send()` returns raw API JSON in `HTTPResponse.data`. The supervisor parses it with `parse_api_response()` (from `output_formatter.h`), which yields a `ParsedResponse` with `response_content` (the assistant's text). The codebase does not use JSON mode or structured output — all LLM responses are free text. For the REPLAN action, the supervisor's prompt instructs the LLM to emit a JSON task array inside a fenced code block, which the supervisor extracts with simple delimiter search and parses with cJSON:
```c
ParsedResponse parsed = {0};
parse_api_response(response.data, &parsed);
// parsed.response_content contains the assistant's text
// For REPLAN: extract ```json ... ``` block, parse with cJSON
// For evaluation: plain text decision ("COMPLETE", "CONTINUE", etc.)
```

**Worker capacity**: The supervisor enforces a per-project concurrency limit via `config_get_int("max_workers_per_project", 3)`, following the `max_subagents` pattern. Add `int max_workers_per_project` to `agent_config_t` in `config.h` with a default of 3 and a hard cap of 20 (matching `SUBAGENT_HARD_CAP`). The decision engine checks `active_workers < max_workers` before choosing SPAWN_WORKER.

**Orchestrator↔Supervisor communication**: The `project_store` is the single source of truth. The supervisor updates `project.status` and `project.summary` directly in SQLite as it progresses. The orchestrator polls with `project_store_get()` — no IPC messages needed for status. For the `project_status` tool, the orchestrator reads the project row plus `task_store_list_by_session()` to build a complete view. Process liveness is checked with `kill(supervisor_pid, 0)`.

**Worker context building**: Before enqueuing a work item, the supervisor builds a context JSON blob containing:
- Project goal (1-2 sentence summary)
- This task's description
- Results from dependency tasks (pulled from completed work items)
- Relevant file paths or hints

### Phase 3: Agent Mode Integration

Wire the supervisor into the existing agent process spawning.

**Modify:**
- `lib/agent/agent.h` — Add `AGENT_MODE_SUPERVISOR = 4` to `AgentMode` enum. Add `const char* supervisor_project_id` to `AgentConfig`.
- `lib/agent/agent.c` — Add `case AGENT_MODE_SUPERVISOR:` to `agent_run()`. This case creates a `Supervisor`, calls `supervisor_run()`, and cleans up. Follows the exact pattern of `AGENT_MODE_WORKER` (lines 205-289).
- `src/ralph/main.c` — Add `--supervisor --project <id>` CLI flags, mirroring `--worker --queue`.

The orchestrator spawns supervisors by forking ralph with `--supervisor --project <project_id>`.

### Phase 4: Orchestrator + Tools

The orchestrator is an in-process module instantiated by the interactive agent.

**New files:**
- `lib/orchestrator/orchestrator.h` — Orchestrator lifecycle, project management API
- `lib/orchestrator/orchestrator.c` — Implementation
- `lib/tools/orchestrator_tool.h` — Tool registration for LLM access
- `lib/tools/orchestrator_tool.c` — Tool implementations

**Tools exposed to the LLM:**
| Tool | Description |
|------|-------------|
| `create_project` | Decompose goal into tasks, create project |
| `list_projects` | Show all projects with status |
| `project_status` | Detailed view of one project's task DAG |
| `start_project` | Spawn supervisor for a project |
| `pause_project` | Signal supervisor to pause |
| `cancel_project` | Kill supervisor and workers |

**`create_project` implementation detail**: This tool performs task decomposition in-process (inside the interactive agent, not in a supervisor). Steps:
1. Create the `Project` row in `project_store` with status ACTIVE and a generated `session_id`.
2. Build a one-shot LLM call via `llm_client_send()` with a planning prompt: the project goal plus instructions to return a JSON array of `{"description", "depends_on": [indexes]}` objects inside a fenced code block.
3. Parse the response with `parse_api_response()` → extract the JSON block → parse with cJSON.
4. Create each task via `task_store_create_task(store, session_id, description, priority, NULL, &out_id)`, collecting the generated IDs.
5. Wire dependencies via `task_store_add_dependency(store, task_id, blocked_by_id)` in a second pass. Note: `task_store_replace_session_tasks()` does NOT insert dependency edges — dependencies must be added individually after task creation.
6. Return the project ID and task count to the LLM as the tool result (using `tool_result_builder`).

The tool follows existing patterns: global `Services*` pointer set during registration (see `memory_tool.c`), parameters extracted with `extract_string_param()`, results via `tool_result_builder_create/set_success/finalize`.

**Modify:**
- `lib/agent/session.h` — Add forward declaration for `Orchestrator`
- `lib/agent/session.c` — Init orchestrator during `session_init()`, register tools
- `mk/lib.mk` — Add `LIB_ORCHESTRATOR_SOURCES`

### Phase 5: Worker Result Capture

The current worker mode reports "Task completed successfully" as the result. Workers need to capture the LLM's final response as the result so supervisors can evaluate what was done.

**Modify:**
- `lib/agent/agent.c` (worker loop) — After `session_process_message()`, extract the last assistant message from `conversation.data[conversation.count-1].content` and pass it to `work_queue_complete()` as the result.

### Phase 6: Recovery

Handle crashes and restarts.

- **Stale supervisor detection**: On orchestrator init, scan `project_store` for projects with `supervisor_pid != 0`. Check each PID with `kill(pid, 0)`. If the process is gone (errno == ESRCH), clear the PID and set status to PAUSED for user-initiated restart. To guard against PID recycling on long-lived systems, add a `supervisor_started_at` column (int64, millis) to the projects schema. On recovery, treat any supervisor older than 1 hour with a dead PID as definitively stale.
- **Zombie reaping**: The orchestrator must call `waitpid(pid, &status, WNOHANG)` for each tracked supervisor during its polling cycle, following the pattern in `subagent_poll_all()`. On graceful shutdown (`cancel_project`), use the established sequence: `kill(pid, SIGTERM)` → `usleep(100ms)` → `kill(pid, SIGKILL)` → `waitpid(pid, &status, 0)`.
- **Worker retry**: Already handled by `work_queue` — failed items retry up to `max_attempts`.
- **Project resume**: `start_project` on an existing project picks up from current task state.

## File Summary

| Action | File | Purpose |
|--------|------|---------|
| Create | `lib/db/project_store.h` | Project struct + CRUD API |
| Create | `lib/db/project_store.c` | SQLite-backed implementation |
| Create | `lib/orchestrator/supervisor.h` | Supervisor decision engine API |
| Create | `lib/orchestrator/supervisor.c` | Supervisor loop + LLM evaluation |
| Create | `lib/orchestrator/orchestrator.h` | Multi-project management API |
| Create | `lib/orchestrator/orchestrator.c` | Supervisor spawning + lifecycle |
| Create | `lib/tools/orchestrator_tool.h` | LLM tool registration |
| Create | `lib/tools/orchestrator_tool.c` | Tool implementations |
| Create | `test/db/test_project_store.c` | Project store unit tests |
| Create | `test/orchestrator/test_supervisor.c` | Supervisor decision tests |
| Modify | `lib/services/services.h` | Add `project_store` field |
| Modify | `lib/services/services.c` | Wire project_store in factory |
| Modify | `lib/agent/agent.h` | Add `AGENT_MODE_SUPERVISOR` |
| Modify | `lib/agent/agent.c` | Add supervisor case + worker result capture |
| Modify | `src/ralph/main.c` | Add `--supervisor --project` flags |
| Modify | `lib/util/config.h` | Add `max_workers_per_project` field |
| Modify | `lib/util/config.c` | Default, JSON load/save, `config_get_int` case |
| Modify | `mk/lib.mk` | Add `LIB_ORCHESTRATOR_SOURCES` |
| Modify | `mk/tests.mk` | Add test targets |

## Key Reuse

These existing primitives are used **as-is** with no modification:
- `task_store` — Task DAG with dependencies, `task_store_list_ready()`, `task_store_add_dependency()`, `task_store_replace_session_tasks()`
- `work_queue` / `worker_spawn()` — Work distribution and retry
- `sqlite_dal` — Database access patterns (config, binders, mappers, transactions)
- `parse_api_response()` — Extract `ParsedResponse.response_content` from raw LLM JSON
- `llm_client_send()` — Supervisor's LLM calls for evaluation
- `config_get()` / `config_get_int()` — API credentials and `max_workers_per_project`
- `tool_result_builder` — Consistent tool result formatting for orchestrator tools

## Verification

1. **Project store**: Build and run `test/test_project_store` — CRUD, status transitions, session_id scoping
2. **Supervisor decisions**: Build and run `test/test_supervisor` — mock LLM responses, verify state machine transitions (SPAWN when ready, WAIT when busy, COMPLETE when done, REPLAN on failure)
3. **Integration**: Create a project via the interactive REPL, verify supervisor spawns, workers execute tasks, supervisor evaluates completion
4. **Recovery**: Kill a supervisor process, restart ralph, verify it detects the stale PID and can resume
5. **Memory safety**: `make check-valgrind` on all new test binaries (exclude subagent/fork tests per existing policy)
