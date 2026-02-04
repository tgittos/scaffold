# Library Migration Plan for Ralph

## Overview

Complete the migration from monolithic `src/` to library-based `lib/` architecture. Each session is a self-contained unit that leaves the codebase buildable with passing tests.

**Current State:**
- `lib/`: 100 files (46 .c, 54 .h), ~20k lines
- `src/`: 96 files (56 .c, 40 .h), ~41k lines

**Goal:** Move implementations to `lib/`, leaving only entry points in `src/`.

---

## Session 12: LLM Providers

**Move:** Provider implementations.

| File | Destination |
|------|-------------|
| `src/llm/providers/openai_provider.c` | `lib/llm/providers/` |
| `src/llm/providers/anthropic_provider.c` | `lib/llm/providers/` |
| `src/llm/providers/local_ai_provider.c` | `lib/llm/providers/` |
| `src/llm/providers/openai_embedding_provider.c` | `lib/llm/providers/` |
| `src/llm/providers/local_embedding_provider.c` | `lib/llm/providers/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh openai_streaming`

---

## Session 13: LLM Models

**Move:** Model capability definitions.

| File | Destination |
|------|-------------|
| `src/llm/models/response_processing.c/h` | `lib/llm/models/` |
| `src/llm/models/claude_model.c` | `lib/llm/models/` |
| `src/llm/models/gpt_model.c` | `lib/llm/models/` |
| `src/llm/models/qwen_model.c` | `lib/llm/models/` |
| `src/llm/models/deepseek_model.c` | `lib/llm/models/` |
| `src/llm/models/default_model.c` | `lib/llm/models/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh model_tools`

---

## Session 14: Policy Core

**Move:** Foundational policy utilities.

| File | Destination |
|------|-------------|
| `src/policy/rate_limiter.c/h` | `lib/policy/` |
| `src/policy/allowlist.c/h` | `lib/policy/` |
| `src/policy/shell_parser.c/h` | `lib/policy/` |
| `src/policy/shell_parser_cmd.c` | `lib/policy/` |
| `src/policy/shell_parser_ps.c` | `lib/policy/` |

**Changes:**
1. Create `lib/policy/` directory
2. Update `mk/lib.mk`: Add to `LIB_POLICY_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh shell_parser`

---

## Session 15: Policy Advanced

**Move:** File protection and verification.

| File | Destination |
|------|-------------|
| `src/policy/path_normalize.c/h` | `lib/policy/` |
| `src/policy/protected_files.c/h` | `lib/policy/` |
| `src/policy/atomic_file.c/h` | `lib/policy/` |
| `src/policy/verified_file_context.c/h` | `lib/policy/` |
| `src/policy/verified_file_python.c/h` | `lib/policy/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh path_normalize`

---

## Session 16: Policy Gates

**Move:** Approval gate system (security-critical).

| File | Destination |
|------|-------------|
| `src/policy/tool_args.c/h` | `lib/policy/` |
| `src/policy/pattern_generator.c/h` | `lib/policy/` |
| `src/policy/gate_prompter.c/h` | `lib/policy/` |
| `src/policy/approval_gate.c/h` | `lib/policy/` |
| `src/policy/approval_errors.c` | `lib/policy/` |
| `src/policy/subagent_approval.c/h` | `lib/policy/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh approval_gate`

---

## Session 17: Session Module

**Move:** Conversation and session management.

| File | Destination |
|------|-------------|
| `src/session/token_manager.c/h` | `lib/session/` |
| `src/session/conversation_tracker.c/h` | `lib/session/` |
| `src/session/conversation_compactor.c/h` | `lib/session/` |
| `src/session/rolling_summary.c/h` | `lib/session/` |
| `src/session/session_manager.c/h` | `lib/session/` |

**Changes:**
1. Create `lib/session/` directory
2. Update `mk/lib.mk`: Add to `LIB_SESSION_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh conversation_tracker`

---

## Session 18: Final Cleanup

**Tasks:**
1. Audit remaining `src/` files (main.c, ralph.c, network/, mcp/)
2. Resolve coupling issues (see "Known Coupling Issues" section)
3. Update `lib/ralph.h` as comprehensive public API
4. Update `ARCHITECTURE.md` and `CODE_OVERVIEW.md`
5. Final verification: `./scripts/build.sh && ./scripts/run_tests.sh && make check-valgrind`

**Namespace Rename:** Remove `ralph_` prefix from lib/ identifiers (73 total) and rename public header.

The library code should use a generic namespace since lib/ is meant to be reusable.

**Public header rename:**
- `lib/ralph.h` → `lib/libagent.h`
- `LIBRALPH_H` guard → `LIBAGENT_H`
- `LIBRALPH_VERSION_*` macros → `LIBAGENT_VERSION_*`
- Update all `#include "ralph.h"` references in src/

Current `ralph_` prefixed identifiers to rename:

| Category | Count | Examples |
|----------|-------|----------|
| Functions | 15 | `ralph_agent_init` → `agent_init`, `ralph_services_create_default` → `services_create_default` |
| Types | 6 | `RalphAgent` → `Agent`, `RalphServices` → `Services`, `RalphAgentConfig` → `AgentConfig` |
| Macros | 48 | `ralph_tools_init` → `tools_init`, `ralph_output_streaming_text` → `output_streaming_text` |
| Enum values | 4 | `RALPH_AGENT_MODE_INTERACTIVE` → `AGENT_MODE_INTERACTIVE` |

**Files requiring updates:**
- `lib/ralph.h` (3 macros, 3 typedefs)
- `lib/agent/agent.h` (2 types, 1 enum, 4 enum values, 9 functions)
- `lib/agent/agent.c` (5 function definitions)
- `lib/services/services.h` (1 type, 7 function declarations)
- `lib/services/services.c` (7 function definitions)
- `lib/tools/tools.h` (11 macros, 3 inline functions)
- `lib/ui/output.h` (14 macros)
- `lib/ui/spinner.h` (3 macros)
- `lib/ui/json_output.h` (6 macros)
- `lib/ui/repl.h` (2 functions)
- `lib/ui/repl.c` (2 function definitions)
- `lib/ipc/pipe_notifier.h` (6 macros)
- `lib/ipc/agent_identity.h` (6 macros)
- `lib/ipc/message_store.h` (5 macros)
- `lib/workflow/workflow.c` (1 static function)

---

## Critical Files

| File | Purpose |
|------|---------|
| `mk/lib.mk` | Library build rules - update after each session |
| `mk/sources.mk` | Source definitions - remove migrated files |
| `mk/tests.mk` | Test definitions - update paths as needed |
| `lib/ralph.h` | Public API - expand as modules migrate |
| `lib/services/services.h` | DI container - update includes |
| `lib/util/` | Generic utilities (ptrarray.h, darray.h, uuid_utils) |
| `lib/db/` | Database abstraction (sqlite_dal, used by IPC and workflow) |

---

## Known Coupling Issues

**RESOLVED in Session 7b:**

1. **config.h** - Removed `subagent_manager_init()` which used `config_get()`. Callers now use `subagent_manager_init_with_config()` directly with explicit values.

2. **async_executor.h** - Replaced direct `async_executor_get_active()`/`async_executor_notify_subagent_spawned()` calls with a callback interface (`SubagentSpawnCallback`). CLI can set this via `subagent_manager_set_spawn_callback()`.

**Files that must stay in src/ (CLI-specific):**
- `src/core/ralph.h/.c` - CLI orchestration layer defining `RalphSession`

---

## Dependency Order

```
Session 1: IPC primitives (pipe_notifier, agent_identity) ✓ COMPLETE
    ↓
Session 2: Database foundation (sqlite_dal, uuid_utils, darray, ptrarray) ✓ COMPLETE
    ↓
Session 3: IPC message_store ✓ COMPLETE
    ↓
Session 4: IPC completion (message_poller) ✓ COMPLETE
    ↓
Session 5: UI terminal utilities (terminal, spinner) ✓ COMPLETE
    ↓
Session 6: UI output formatting (output_formatter, json_output) ✓ COMPLETE
    ↓
Session 7: UI CLI commands (memory_commands) ✓ COMPLETE
    ↓
Session 8 (was 8-9, 16, 18): Tools System + All Tools (except Python) ✓ COMPLETE
    ↓
Session 7b: Utility Migration (fixes lib/tools/ → src/ dependencies) ✓ COMPLETE
    ↓
Session 8-9: Vector DB + Services (depends on lib/db/sqlite_dal) ✓ COMPLETE
    ↓
Session 10: LLM Core (llm_provider, model_capabilities) ✓ COMPLETE
    ↓
Session 11: LLM Embeddings (embeddings, embedding_provider, embeddings_service) ✓ COMPLETE
    ↓
Session 12-13: LLM Providers and Models
    ↓
Session 14-16: Policy (depends on Tools for types)
    ↓
Session 17: Session (depends on DB, LLM)
    ↓
Session 18: Cleanup
```

---

## Per-Session Checklist

For each session:
1. [ ] Move files to `lib/` destination
2. [ ] Update includes in moved files (relative paths within lib/)
3. [ ] Update `mk/lib.mk` with new sources
4. [ ] Update `mk/sources.mk` to remove migrated files
5. [ ] Update includes in `src/` files that referenced moved headers
6. [ ] Build: `./scripts/build.sh clean && ./scripts/build.sh`
7. [ ] Test: `./scripts/run_tests.sh`
8. [ ] Commit with descriptive message
