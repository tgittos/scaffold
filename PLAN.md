# Scaffold Implementation Plan

Directed dependency graph of workable chunks. Each chunk is independently buildable, testable, and committable. Later chunks depend only on completed earlier chunks.

Notation: `[NEW]` = new file, `[MOD]` = modify existing file.

---

## Chunk 1: Goal Store [DONE]

SQLite-backed goal persistence following `task_store` / `sqlite_dal` patterns.

**Design note**: `description` and `summary` are heap-allocated (`char*`), not fixed buffers — matches `task_store` pattern and avoids silent truncation.

**Files:**
- `[NEW] lib/db/goal_store.h` — `Goal` struct, `GoalStatus` enum, CRUD API
- `[NEW] lib/db/goal_store.c` — Implementation using `sqlite_dal`
- `[NEW] test/db/test_goal_store.c` — Unit tests
- `[MOD] mk/lib.mk` — Add `goal_store.c` to `LIB_DB_SOURCES`
- `[MOD] mk/tests.mk` — Add `test_goal_store` target (standalone, `def_test` with sqlite_dal + uuid_utils + app_home deps — same pattern as `test_task_store`)

**API surface:**
```c
goal_store_t* goal_store_create(const char* db_path);
void goal_store_destroy(goal_store_t* store);
int goal_store_insert(goal_store_t* store, const char* name,
                      const char* description, const char* goal_state_json,
                      const char* queue_name, char* out_id);
Goal* goal_store_get(goal_store_t* store, const char* id);
int goal_store_update_status(goal_store_t* store, const char* id, GoalStatus status);
int goal_store_update_world_state(goal_store_t* store, const char* id, const char* world_state_json);
int goal_store_update_summary(goal_store_t* store, const char* id, const char* summary);
int goal_store_update_supervisor(goal_store_t* store, const char* id, pid_t pid, int64_t started_at);
Goal** goal_store_list_all(goal_store_t* store, size_t* count);
Goal** goal_store_list_by_status(goal_store_t* store, GoalStatus status, size_t* count);
void goal_free(Goal* goal);
void goal_free_list(Goal** goals, size_t count);
```

**Tests:** CRUD lifecycle, status transitions, world state JSON round-trip, supervisor PID tracking, list queries with status filtering.

**Depends on:** Nothing (standalone store).

---

## Chunk 2: Action Store [DONE]

SQLite-backed action persistence with hierarchy and GOAP readiness queries.

**Design note**: `description` is heap-allocated (`char*`), not `char[512]`. Readiness checking (preconditions against world state) is done in C after fetching all pending actions — parse JSON arrays with cJSON, check each assertion key exists and is true in world state object.

**Files:**
- `[NEW] lib/db/action_store.h` — `Action` struct, `ActionStatus` enum, CRUD + readiness API
- `[NEW] lib/db/action_store.c` — Implementation using `sqlite_dal`
- `[NEW] test/db/test_action_store.c` — Unit tests
- `[MOD] mk/lib.mk` — Add `action_store.c` to `LIB_DB_SOURCES`
- `[MOD] mk/tests.mk` — Add `test_action_store` target (standalone, same deps pattern as `test_task_store` plus cJSON for precondition/effect JSON parsing)

**API surface:**
```c
action_store_t* action_store_create(const char* db_path);
void action_store_destroy(action_store_t* store);
int action_store_insert(action_store_t* store, const char* goal_id,
                        const char* parent_action_id, const char* description,
                        const char* preconditions_json, const char* effects_json,
                        bool is_compound, const char* role, char* out_id);
Action* action_store_get(action_store_t* store, const char* id);
int action_store_update_status(action_store_t* store, const char* id,
                               ActionStatus status, const char* result);
Action** action_store_list_ready(action_store_t* store, const char* goal_id,
                                 const char* world_state_json, size_t* count);
Action** action_store_list_by_goal(action_store_t* store, const char* goal_id, size_t* count);
Action** action_store_list_children(action_store_t* store, const char* parent_action_id, size_t* count);
int action_store_count_by_status(action_store_t* store, const char* goal_id, ActionStatus status);
int action_store_skip_pending(action_store_t* store, const char* goal_id);
void action_free(Action* action);
void action_free_list(Action** actions, size_t count);
```

**Tests:** CRUD lifecycle, parent/child hierarchy, readiness with various world states (no preconditions = always ready, partial match = not ready, full match = ready), compound vs primitive distinction, skip_pending batch update, count_by_status.

**Depends on:** Nothing (standalone store). Can be built in parallel with Chunk 1.

---

## Chunk 3: Services Wiring [DONE]

Connect new stores to the DI container so tools and agents can access them.

**Files:**
- `[MOD] lib/services/services.h` — Add `goal_store_t* goal_store` and `action_store_t* action_store` fields + accessor functions
- `[MOD] lib/services/services.c` — Create/destroy in `services_create_default()` / `services_destroy()`. Both stores share a single DB file (`scaffold.db` in app home) to reduce WAL contention vs. separate files per store.

**Tests:** Existing test suite passes. `services_create_default()` creates and destroys new stores without leaking. Verify with valgrind on `test_task_store` and other existing libagent-linked tests.

**Depends on:** Chunks 1, 2.

---

## Chunk 4: Worker Result Capture [DONE]

Workers pass the LLM's actual final response to `work_queue_complete()` instead of the static `"Task completed successfully"` string. Supervisors need actual results to verify effects.

**Files:**
- `[MOD] lib/agent/agent.c` — In the `AGENT_MODE_WORKER` case, after `session_process_message()` returns 0, extract the last assistant message from `agent->session.session_data.conversation` (`.data[count-1].content`) and pass it to `work_queue_complete()`. Handle edge case: if conversation is empty, fall back to static string.

**Tests:** Build and run existing worker-related tests. Add a focused test if feasible (may require mock session — evaluate during implementation).

**Depends on:** Nothing (modifies existing code, no new stores needed).

---

## Chunk 5: Worker Exit on Empty Queue [DONE]

Workers exit cleanly when the queue is empty instead of sleeping forever. This makes workers ephemeral — they die after draining their queue, which is essential for the supervisor to know when dispatched work is complete.

**Files:**
- `[MOD] lib/agent/agent.c` — In the `AGENT_MODE_WORKER` while loop, when `work_queue_claim()` returns NULL, `break` instead of `usleep(1000000); continue`.

**Tests:** Existing worker tests still pass. Worker process exits with 0 after processing all items.

**Depends on:** Nothing. Can be built in parallel with Chunks 1-4.

---

## Chunk 6: System Prompt Pass-Through [DONE]

Wire the `system_prompt` parameter through `worker_spawn()` to the child process. Currently accepted but unused (`(void)system_prompt`).

**Approach:** Write prompt to a temp file, pass path via `--system-prompt-file <path>` CLI flag. Parent cleans up temp file after child exits (in `worker_stop()` and the zombie reaping path). Child reads file during `agent_init()` / config loading and deletes it after reading.

**Files:**
- `[MOD] lib/workflow/workflow.c` — `worker_spawn()`: write system_prompt to temp file, add `--system-prompt-file` args to execv. Store temp file path in `WorkerHandle` for cleanup. `worker_stop()` / `worker_handle_free()`: unlink temp file if it still exists.
- `[MOD] lib/workflow/workflow.h` — Add `char system_prompt_file[256]` to `WorkerHandle` struct.
- `[MOD] src/scaffold/main.c` — Parse `--system-prompt-file <path>` flag, read contents, set `config.worker_system_prompt`.
- `[MOD] lib/agent/session.c` or `lib/agent/session_configurator.c` — If `worker_system_prompt` is set in config, use it as the system prompt override instead of the default.

**Tests:** Build scaffold binary. Verify `worker_spawn("queue", "custom prompt")` creates temp file and child receives it. Verify temp file is cleaned up after child exits.

**Depends on:** Nothing. Can be built in parallel with Chunks 1-5.

---

## Chunk 7: Config — max_workers_per_goal [DONE]

Add the `max_workers_per_goal` configuration key.

**Files:**
- `[MOD] lib/util/config.h` — Add `int max_workers_per_goal` field to `agent_config_t`.
- `[MOD] lib/util/config.c` — Default value (3), JSON load/save, wire into `config_get_int()`.

**Tests:** Extend `test_config` to verify the new key loads from JSON and falls back to default.

**Depends on:** Nothing.

---

## Chunk 8: GOAP Tools [DONE]

Tool implementations that supervisors (and the scaffold agent during decomposition) call via the LLM. These are the core GOAP operations exposed as tools.

**Files:**
- `[NEW] lib/tools/goap_tools.h` — Tool registration function + `goap_tools_set_services()`
- `[NEW] lib/tools/goap_tools.c` — Tool implementations (9 tools)
- `[NEW] test/tools/test_goap_tools.c` — Unit tests with injected stores
- `[MOD] mk/lib.mk` — Add `goap_tools.c` to `LIB_TOOLS_SOURCES`
- `[MOD] mk/tests.mk` — Add `test_goap_tools` target (`def_test_lib`)

**Tools:**

| Tool | What it does |
|------|-------------|
| `goap_get_goal` | Read goal: description, goal_state, world_state, status |
| `goap_list_actions` | List actions for a goal (filter by status, parent) |
| `goap_create_goal` | Create goal with name, description, goal_state assertions |
| `goap_create_actions` | Batch-insert actions with preconditions/effects |
| `goap_update_action` | Update action status/result |
| `goap_dispatch_action` | Enqueue primitive to work queue + spawn worker (enforce capacity cap) |
| `goap_update_world_state` | Set boolean assertions in goal's world_state |
| `goap_check_complete` | Test `world_state ⊇ goal_state`, return boolean |
| `goap_get_action_results` | Read completed action results from store (truncate to configurable max length for context safety) |

**Implementation patterns:** Global `Services*` via `goap_tools_set_services()` (same as `memory_tool_set_services`). Parameters via `extract_string_param()`. Results via `tool_result_builder`. `goap_dispatch_action` internally calls `worker_spawn()` with role-based system prompt and enforces `config_get_int("max_workers_per_goal", 3)`.

**Design note on result truncation**: `goap_get_action_results` truncates each result to a configurable max (default 4000 chars) to prevent context blowup in supervisors. Full results remain in the store.

**Tests:** Create stores with `services_create_empty()`, inject test data, verify each tool returns correct JSON. Test dispatch enforces worker cap. Test readiness filtering. Test batch action creation.

**Depends on:** Chunks 1, 2, 3, 7.

---

## Chunk 9: Tool Registration + Service Wiring [DONE]

Wire GOAP tools into the tool registry for scaffold agents.

**Files:**
- `[MOD] lib/tools/builtin_tools.c` — Add `register_goap_tools()` call, conditioned on scaffold mode (check `app_home_get_app_name()` or an env var like the existing `AGENT_IS_SUBAGENT` pattern)
- `[MOD] lib/agent/agent.c` — Add `goap_tools_set_services()` call in the service wiring block (after `memory_tool_set_services`)

**Tests:** Build scaffold binary. Verify GOAP tools appear in the tool registry when running as scaffold. Verify they do NOT appear when running as ralph.

**Depends on:** Chunk 8.

---

## Chunk 10: Supervisor Event Loop [DONE]

The supervisor agent mode — a REPL without stdin that progresses a goal via GOAP tool calls, sleeping between worker dispatches and waking on worker completion.

**Files:**
- `[NEW] lib/orchestrator/supervisor.h` — `supervisor_run()` API
- `[NEW] lib/orchestrator/supervisor.c` — Implementation
- `[MOD] mk/lib.mk` — Add `LIB_ORCHESTRATOR_SOURCES` section with `supervisor.c`

**Lifecycle:**
1. `supervisor_run(session, goal_id)` reads goal state from stores via GOAP tools.
2. Builds a status summary message, calls `session_process_message()`. The LLM examines state and calls GOAP tools — decompose compound actions, dispatch primitives, verify effects.
3. After the LLM's turn completes (workers dispatched, nothing to do until they finish), enters event loop: `select()` on message poller fd only (no stdin fd).
4. Worker completion arrives via `subagent_notify_parent()` → message poller detects → pipe notification → `select()` unblocks.
5. Fetch message, inject as system notification into conversation history, call `session_continue()`. LLM sees "worker X completed" and responds with more GOAP tool calls.
6. Repeat 3-5 until `goap_check_complete` returns true → clean exit.
7. If context fills up or supervisor errors, exit with distinct code so scaffold knows to respawn.

**Tests:** Unit tests are hard here (requires mocked LLM). Focus on integration testing in Chunk 14. The supervisor module itself should be testable with a mock session that simulates tool call responses.

**Depends on:** Chunks 8, 9.

---

## Chunk 11: Agent Mode — Supervisor [DONE]

Wire the supervisor into the agent lifecycle and scaffold CLI.

**Files:**
- `[MOD] lib/agent/agent.h` — Add `AGENT_MODE_SUPERVISOR = 4` to `AgentMode` enum. Add `const char* supervisor_goal_id` to `AgentConfig`.
- `[MOD] lib/agent/agent.c` — Add `case AGENT_MODE_SUPERVISOR:` in `agent_run()`. Creates supervisor, calls `supervisor_run()`, cleans up. Same signal handler pattern as worker.
- `[MOD] src/scaffold/main.c` — Add `--supervisor --goal <id>` argument parsing (same pattern as `--worker --queue <name>`).

**Tests:** Build scaffold with `--supervisor --goal <id>`. Verify it enters supervisor mode and reads from stores. (Full lifecycle test in Chunk 14.)

**Depends on:** Chunk 10.

---

## Chunk 12: Orchestrator — Supervisor Spawning + Monitoring [DONE]

The scaffold-agent-level code that spawns supervisors (one per goal) and monitors their progress.

**Files:**
- `[NEW] lib/orchestrator/orchestrator.h` — Orchestrator lifecycle API: spawn supervisor, check liveness, respawn
- `[NEW] lib/orchestrator/orchestrator.c` — Implementation: fork/exec scaffold as `--supervisor --goal <id>`, PID tracking, liveness checks with `kill(pid, 0)`, zombie reaping with `waitpid(WNOHANG)`
- `[MOD] mk/lib.mk` — Add `orchestrator.c` to `LIB_ORCHESTRATOR_SOURCES`

**Key functions:**
```c
// Spawn a supervisor process for a goal. Stores PID in goal_store.
int orchestrator_spawn_supervisor(goal_store_t* store, const char* goal_id);

// Check if a supervisor is still alive. Clears stale PID if dead.
bool orchestrator_supervisor_alive(goal_store_t* store, const char* goal_id);

// Reap zombie supervisor processes (call periodically from REPL loop).
void orchestrator_reap_supervisors(goal_store_t* store);

// Kill a supervisor and its workers (for cancel_goal).
int orchestrator_kill_supervisor(goal_store_t* store, const char* goal_id);
```

**Tests:** Spawn a subprocess, verify PID tracking, verify liveness detection after kill.

**Depends on:** Chunks 1, 3, 11.

---

## Chunk 13: Orchestrator Tools + execute_plan [DONE]

Scaffold-agent-facing tools for goal lifecycle management.

**Files:**
- `[NEW] lib/tools/orchestrator_tool.h` — Tool registration + `orchestrator_tool_set_services()`
- `[NEW] lib/tools/orchestrator_tool.c` — Tool implementations
- `[MOD] lib/tools/builtin_tools.c` — Add `register_orchestrator_tools()` (scaffold mode only)
- `[MOD] lib/agent/agent.c` — Add `orchestrator_tool_set_services()` in wiring block
- `[MOD] mk/lib.mk` — Add `orchestrator_tool.c` to `LIB_TOOLS_SOURCES`

**Tools:**

| Tool | What it does |
|------|-------------|
| `execute_plan` | Clear conversation context, return plan text + decomposition instruction as tool result. On the fresh context, the LLM decomposes via GOAP tools and the orchestrator spawns supervisors. |
| `list_goals` | Read all goals from store, format status summary |
| `goal_status` | Detailed view: world state, action tree, worker status |
| `pause_goal` | Signal supervisor to pause (update goal status, let supervisor detect on next check) |
| `cancel_goal` | Kill supervisor + workers via orchestrator |

**`execute_plan` context clear mechanism:** The tool handler calls `cleanup_conversation_history()` + `init_conversation_history()` on the session (same pattern as worker per-item reset at `agent.c:266-267`). It returns a tool result containing the plan text and a system instruction to decompose into goals. Because the conversation history was just cleared, the LLM's next turn starts with only this tool result as context — no prior conversation bloat.

**Tests:** Unit tests with mock stores for list_goals, goal_status, pause_goal, cancel_goal. execute_plan is best tested in integration (Chunk 14).

**Depends on:** Chunks 8, 9, 12.

---

## Chunk 14: Slash Commands [DONE]

Direct state inspection commands that bypass the LLM.

**Files:**
- `[MOD] lib/ui/slash_commands.c` (or a new `lib/ui/goal_commands.c` + `[MOD] lib/ui/slash_commands.c` to register it) — Implement `/goals`, `/tasks <goal_id>`, `/agents`

| Command | What it does |
|---------|-------------|
| `/goals` | List all goals with status, world state progress (X/Y assertions true) |
| `/tasks [goal_id]` | Show action tree: compound parents → primitive children, indented. Status, role, effects per action. |
| `/agents` | Show running supervisors (PID, goal, uptime) and workers |

**Pattern:** Same as existing `/memory` commands in `memory_commands.c` — read directly from stores, format with terminal helpers, no LLM round-trip.

**Tests:** Add to existing `test_slash_commands` or create focused test for command parsing.

**Depends on:** Chunks 1, 2, 3.

---

## Chunk 15: Role-Based System Prompts

Worker system prompt templates loaded from configurable directory with built-in fallbacks.

**Files:**
- `[NEW] lib/orchestrator/role_prompts.h` — `role_prompt_load(const char* role)` API
- `[NEW] lib/orchestrator/role_prompts.c` — Load from `~/.local/scaffold/prompts/<role>.md`, fall back to built-in defaults for standard roles, generic worker prompt for unknown roles
- `[MOD] mk/lib.mk` — Add to `LIB_ORCHESTRATOR_SOURCES`
- `[MOD] lib/tools/goap_tools.c` — `goap_dispatch_action` uses `role_prompt_load()` to select system prompt before calling `worker_spawn()`

**Built-in roles:** `implementation`, `code_review`, `architecture_review`, `design_review`, `pm_review`, `testing`. Each has a ~200-word focused system prompt embedded as a string constant.

**Tests:** Verify known role returns built-in prompt, unknown role returns generic prompt, file override takes precedence.

**Depends on:** Chunk 8.

---

## Chunk 16: Recovery + Respawn

Crash resilience: detect dead supervisors, respawn them, handle stale state on startup.

**Files:**
- `[MOD] lib/orchestrator/orchestrator.c` — Add:
  - `orchestrator_check_stale()`: On startup, scan goal_store for goals with `supervisor_pid != 0`, check liveness, clear stale PIDs (use `supervisor_started_at` + 1hr threshold to guard against PID recycling)
  - `orchestrator_respawn_dead()`: For active goals without a running supervisor, respawn. Called periodically from the scaffold's REPL loop or monitoring poll.
- `[MOD] lib/agent/agent.c` or `lib/agent/repl.c` — Hook `orchestrator_reap_supervisors()` + `orchestrator_respawn_dead()` into the REPL's periodic check cycle (same place `subagent_poll_all()` is called, if applicable).

**Tests:**
- Kill a supervisor PID, verify `orchestrator_supervisor_alive()` returns false and `orchestrator_respawn_dead()` spawns a new one.
- Create a goal with stale PID (process doesn't exist), verify `orchestrator_check_stale()` clears it.

**Depends on:** Chunk 12.

---

## Chunk 17: Integration Testing

End-to-end validation of the full orchestration pipeline.

**Tests (manual + scripted):**
1. Scaffold REPL → user provides plan → LLM calls `execute_plan` → context clears → LLM decomposes into goals + actions via GOAP tools → supervisors spawn
2. Supervisor reads goal state → decomposes compound actions → dispatches primitives → workers execute → workers die → supervisor wakes on completion → verifies effects → updates world state → dispatches next batch
3. Goal completes when `world_state ⊇ goal_state` → supervisor exits cleanly → scaffold reports completion
4. Kill supervisor mid-goal → scaffold detects dead PID → respawns → new supervisor continues from store state
5. `/goals`, `/tasks`, `/agents` show correct state at each stage
6. Valgrind on new test binaries (exclude fork/exec tests per existing policy)

**Depends on:** All previous chunks.

---

## Dependency Graph

```
Chunk 1 (goal_store)  ──┐
                         ├── Chunk 3 (services wiring) ──┐
Chunk 2 (action_store) ──┘                               │
                                                          ├── Chunk 8 (GOAP tools) ──── Chunk 9 (registration) ──── Chunk 10 (supervisor loop) ──── Chunk 11 (agent mode)
Chunk 7 (config) ─────────────────────────────────────────┘                                                                                           │
                                                                                                                                                       │
Chunk 4 (worker result) ── standalone                                                                                            Chunk 12 (orchestrator) ── Chunk 13 (orchestrator tools)
Chunk 5 (worker exit)   ── standalone                                                                                                  │
Chunk 6 (system prompt) ── standalone                                                                                            Chunk 16 (recovery)
                                                                                                                                       │
Chunks 1,2,3 ────────── Chunk 14 (slash commands)                                                                               Chunk 17 (integration)
Chunk 8 ─────────────── Chunk 15 (role prompts)
```

## Parallelism

These chunks can be worked on concurrently:
- **Wave 1:** Chunks 1, 2, 4, 5, 6, 7 (all independent)
- **Wave 2:** Chunks 3, 14 (need stores from Wave 1)
- **Wave 3:** Chunks 8, 15 (need services wiring + config)
- **Wave 4:** Chunks 9, 10 (need GOAP tools)
- **Wave 5:** Chunks 11, 12 (need supervisor + agent mode)
- **Wave 6:** Chunks 13, 16 (need orchestrator)
- **Wave 7:** Chunk 17 (integration — needs everything)

## Estimated Sizes

| Chunk | New LoC (approx) | Files touched |
|-------|-------------------|---------------|
| 1. Goal Store | ~400 | 5 |
| 2. Action Store | ~550 | 5 |
| 3. Services Wiring | ~40 | 2 |
| 4. Worker Result | ~15 | 1 |
| 5. Worker Exit | ~5 | 1 |
| 6. System Prompt | ~80 | 4 |
| 7. Config | ~15 | 2 |
| 8. GOAP Tools | ~700 | 5 |
| 9. Registration | ~20 | 2 |
| 10. Supervisor Loop | ~300 | 3 |
| 11. Agent Mode | ~40 | 3 |
| 12. Orchestrator | ~250 | 3 |
| 13. Orchestrator Tools | ~350 | 5 |
| 14. Slash Commands | ~200 | 2 |
| 15. Role Prompts | ~250 | 4 |
| 16. Recovery | ~150 | 2 |
| 17. Integration | ~100 (test scripts) | — |

**Total: ~3,500 new LoC** across ~48 file touches (17 new files, ~31 modifications).
