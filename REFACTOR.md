# Library Migration Plan for Ralph

## Overview

Complete the migration from monolithic `src/` to library-based `lib/` architecture. Each session is a self-contained unit that leaves the codebase buildable with passing tests.

**Current State:**
- `lib/`: 130 files (67 .c, 63 .h)
- `src/`: 66 files (35 .c, 31 .h)

**Goal:** Move implementations to `lib/`, leaving only entry points in `src/`.

---

## Next: Policy Gates

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

## Session Module

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

## Network Module

**Move:** HTTP client and API communication layer.

| File | Destination |
|------|-------------|
| `src/network/http_client.c/h` | `lib/network/` |
| `src/network/api_common.c/h` | `lib/network/` |
| `src/network/api_error.c/h` | `lib/network/` |
| `src/network/streaming.c/h` | `lib/network/` |
| `src/network/embedded_cacert.c/h` | `lib/network/` |

**Changes:**
1. Create `lib/network/` directory
2. Update `mk/lib.mk`: Add `LIB_NETWORK_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh http`

---

## MCP Module

**Move:** Model Context Protocol plugin system.

| File | Destination |
|------|-------------|
| `src/mcp/mcp_client.c/h` | `lib/mcp/` |
| `src/mcp/mcp_transport.c/h` | `lib/mcp/` |
| `src/mcp/mcp_transport_stdio.c` | `lib/mcp/` |
| `src/mcp/mcp_transport_http.c` | `lib/mcp/` |

**Changes:**
1. Create `lib/mcp/` directory
2. Update `mk/lib.mk`: Add `LIB_MCP_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh mcp_client`

---

## Utils Module

**Move:** Core utilities needed by the agent.

| File | Destination |
|------|-------------|
| `src/utils/config.c/h` | `lib/util/` |
| `src/utils/prompt_loader.c/h` | `lib/util/` |
| `src/utils/ralph_home.c/h` | `lib/util/` |
| `src/utils/context_retriever.c/h` | `lib/util/` |
| `src/utils/pdf_processor.c/h` | `lib/util/` |

**Changes:**
1. Update `mk/lib.mk`: Add to `LIB_UTIL_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh config`

---

## Messaging Module

**Move:** Notification formatting for IPC.

| File | Destination |
|------|-------------|
| `src/messaging/notification_formatter.c/h` | `lib/ipc/` |

**Changes:**
1. Update `mk/lib.mk`: Add to `LIB_IPC_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh notification_formatter`

---

## Core Module

**Move:** Core agent execution infrastructure.

| File | Destination |
|------|-------------|
| `src/core/async_executor.c/h` | `lib/agent/` |
| `src/core/tool_executor.c/h` | `lib/agent/` |
| `src/core/streaming_handler.c/h` | `lib/agent/` |
| `src/core/context_enhancement.c/h` | `lib/agent/` |
| `src/core/recap.c/h` | `lib/agent/` |

**Changes:**
1. Update `mk/lib.mk`: Add to `LIB_AGENT_SOURCES`

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh ralph`

---

## Final Cleanup

**Remaining in src/ (CLI entry point and proprietary):**
- `src/core/main.c` - CLI entry point
- `src/core/ralph.c/h` - CLI orchestration layer
- `src/tools/python_tool.c/h` - Proprietary Python tool system
- `src/tools/python_tool_files.c/h` - Proprietary Python files

**Tasks:**
1. Verify all core agent functionality is in lib/
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
Session 12: LLM Providers (openai, anthropic, local_ai, embedding providers) ✓ COMPLETE
    ↓
Session 13: LLM Models (response_processing, claude/gpt/qwen/deepseek/default) ✓ COMPLETE
    ↓
Policy Core (rate_limiter, allowlist, shell_parser) ✓ COMPLETE
    ↓
Policy Advanced (path_normalize, protected_files, atomic_file, verified_file) ✓ COMPLETE
    ↓
Policy Gates (approval_gate, tool_args, pattern_generator, gate_prompter, subagent_approval)
    ↓
Session Module (token_manager, conversation_tracker, compactor, rolling_summary, session_manager)
    ↓
Network Module (http_client, api_common, api_error, streaming, embedded_cacert)
    ↓
MCP Module (mcp_client, mcp_transport, transports)
    ↓
Utils Module (config, prompt_loader, ralph_home, context_retriever, pdf_processor)
    ↓
Messaging Module (notification_formatter)
    ↓
Core Module (async_executor, tool_executor, streaming_handler, context_enhancement, recap)
    ↓
Final Cleanup
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
