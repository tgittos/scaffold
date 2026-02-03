# Library Migration Plan for Ralph

## Overview

Complete the migration from monolithic `src/` to library-based `lib/` architecture. Each session is a self-contained unit that leaves the codebase buildable with passing tests.

**Current State:**
- `lib/`: 18 files, ~2,300 lines (orchestration layer)
- `src/`: ~180 files, ~20,000 lines (all implementation)

**Goal:** Move implementations to `lib/`, leaving only entry points in `src/`.

---

## Session 8: Tools System Core

**Move:** Tool framework (registry, parsing, execution).

| File | Destination |
|------|-------------|
| `src/tools/tools_system.c/h` | `lib/tools/` |
| `src/tools/tool_format.h` | `lib/tools/` |
| `src/tools/tool_format_openai.c` | `lib/tools/` |
| `src/tools/tool_format_anthropic.c` | `lib/tools/` |
| `src/tools/tool_param_dsl.c/h` | `lib/tools/` |
| `src/tools/tool_result_builder.c/h` | `lib/tools/` |

**Changes:**
1. Move files to `lib/tools/`
2. Update includes to use `lib/util/darray.h` (moved in Session 2)
3. Separate tool registrations into `src/tools/builtin_tools.c`
4. Update `mk/lib.mk`: Add to `LIB_TOOLS_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh tools_system`

---

## Session 9: Simple Tools

**Move:** Self-contained tools.

| File | Destination |
|------|-------------|
| `src/tools/todo_manager.c/h` | `lib/tools/` |
| `src/tools/todo_tool.c/h` | `lib/tools/` |
| `src/tools/todo_display.c/h` | `lib/tools/` |
| `src/tools/messaging_tool.c/h` | `lib/tools/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh todo_manager`

---

## Session 10: Vector Database

**Move:** Vector database infrastructure (lib/db/ already exists from Session 2).

| File | Destination |
|------|-------------|
| `src/db/vector_db.c/h` | `lib/db/` |
| `src/db/hnswlib_wrapper.cpp/h` | `lib/db/` |
| `src/db/metadata_store.c/h` | `lib/db/` |
| `src/db/document_store.c/h` | `lib/db/` |

**Changes:**
1. Move files to `lib/db/`
2. Update `mk/lib.mk`: Add to `LIB_DB_SOURCES`, `LIB_CPP_SOURCES`
3. Update `mk/sources.mk`: Remove from `DB_C_SOURCES`, `DB_CPP_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh vector_db`

---

## Session 11: Database Services

**Move:** Singleton database services.

| File | Destination |
|------|-------------|
| `src/db/vector_db_service.c/h` | `lib/db/` |
| `src/db/task_store.c/h` | `lib/db/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh task_store`

---

## Session 12: LLM Core

**Move:** Provider framework.

| File | Destination |
|------|-------------|
| `src/llm/llm_provider.c/h` | `lib/llm/` |
| `src/llm/model_capabilities.c/h` | `lib/llm/` |

**Changes:**
1. Create `lib/llm/` directory
2. Update `mk/lib.mk`: Add to `LIB_LLM_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh`

---

## Session 13: LLM Embeddings

**Move:** Embedding infrastructure.

| File | Destination |
|------|-------------|
| `src/llm/embeddings.c/h` | `lib/llm/` |
| `src/llm/embedding_provider.c/h` | `lib/llm/` |
| `src/llm/embeddings_service.c/h` | `lib/llm/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh`

---

## Session 14: LLM Providers

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

## Session 15: LLM Models

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

## Session 16: Complex Tools

**Move:** Tools with database dependencies.

| File | Destination |
|------|-------------|
| `src/tools/memory_tool.c/h` | `lib/tools/` |
| `src/tools/vector_db_tool.c/h` | `lib/tools/` |
| `src/tools/pdf_tool.c/h` | `lib/tools/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh memory_tool`

---

## Session 17: Python Tools

**Move:** Python tool infrastructure.

| File | Destination |
|------|-------------|
| `src/tools/python_tool.c/h` | `lib/tools/` |
| `src/tools/python_tool_files.c/h` | `lib/tools/` |
| `src/tools/python_defaults/` | `lib/tools/python_defaults/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh python_tool`

---

## Session 18: Subagent Tools

**Move:** Subagent spawning infrastructure.

| File | Destination |
|------|-------------|
| `src/tools/subagent_tool.c/h` | `lib/tools/` |
| `src/tools/subagent_process.c/h` | `lib/tools/` |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh subagent_tool`

---

## Session 19: Policy Core

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

## Session 20: Policy Advanced

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

## Session 21: Policy Gates

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

## Session 22: Session Module

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

## Session 23: Final Cleanup

**Tasks:**
1. Audit remaining `src/` files (main.c, ralph.c, network/, mcp/, pdf/)
2. Update `lib/ralph.h` as comprehensive public API
3. Update `ARCHITECTURE.md` and `CODE_OVERVIEW.md`
4. Final verification: `./scripts/build.sh && ./scripts/run_tests.sh && make check-valgrind`

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
Session 8-9: Tools Core + Simple (depends on UI for output)
    ↓
Session 10-11: Vector DB + Services (depends on lib/db/sqlite_dal)
    ↓
Session 12-15: LLM (depends on DB for embeddings)
    ↓
Session 16-18: Complex Tools (depends on DB, LLM)
    ↓
Session 19-21: Policy (depends on Tools for types)
    ↓
Session 22: Session (depends on DB, LLM)
    ↓
Session 23: Cleanup
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
