# Architecture Violation Audit

Full audit of every directory in `lib/` and `src/` for violations of the layered architecture defined in `ARCHITECTURE.md`.

## Clean Directories (no violations)

- `lib/mcp/`
- `lib/pdf/`
- `src/core/`
- `src/tools/`
- `lib/services/` (minor include path inconsistencies only)
- `lib/workflow/` (minor include path inconsistencies only)

---

## Violations By Directory

### `lib/db/` - 1 CRITICAL

| File | Line | Violation |
|------|------|-----------|
| `document_store.c` | 6 | `#include "llm/embeddings_service.h"` - DB layer depending on LLM layer |
| `document_store.c` | 321-341 | `document_store_add_text()` calls `embeddings_service_get_vector()` directly instead of receiving embeddings from caller |
| `document_store.c` | 406-419 | `document_store_search_text()` calls `embeddings_service_get_vector()` directly instead of receiving embeddings from caller |

**Root cause:** document_store fetches its own embeddings instead of having vector_db_service (its owner) pass them in. This couples the storage layer to the LLM layer.

---

### `lib/agent/` - 14 violations

| File | Line | Violation |
|------|------|-----------|
| `agent.c` | 11 | `#include "../db/document_store.h"` - direct low-level DB include |
| `agent.c` | 79 | Calls `document_store_set_services()` directly |
| `session.c` | 45 | `#include "../db/task_store.h"` - direct low-level DB include |
| `session.c` | 46 | `#include "../llm/embeddings_service.h"` - direct LLM include |
| `session.c` | 496 | `#include "../network/http_client.h"` - direct network include |
| `session.c` | 602 | Calls `http_post_with_headers()` - bypasses provider abstraction |
| `session.c` | 294, 657 | Calls `get_model_registry()` singleton directly (x2) |
| `tool_executor.c` | 9 | `#include "../network/http_client.h"` - direct network include |
| `tool_executor.c` | 316 | Calls `http_post_with_headers()` - bypasses provider abstraction |
| `tool_executor.c` | 323 | Calls `get_last_api_error()` singleton directly |
| `tool_executor.c` | 395 | Calls `get_model_registry()` singleton directly |
| `recap.c` | 5 | `#include "../network/http_client.h"` - direct network include |
| `recap.c` | 258 | Calls `http_post_streaming()` - bypasses provider abstraction |
| `streaming_handler.c` | 10 | `#include "../network/http_client.h"` - direct network include |

**Root cause:** Agent layer makes direct HTTP calls instead of going through the LLM provider abstraction. Also accesses global singletons instead of using Services DI.

---

### `lib/tools/` - 7 violations

| File | Line | Violation |
|------|------|-----------|
| `memory_tool.c` | 3 | `#include "db/metadata_store.h"` - direct low-level DB include |
| `memory_tool.c` | 154 | Calls `metadata_store_get_instance()` singleton in `execute_remember_tool_call()` |
| `memory_tool.c` | 230 | Calls `metadata_store_get_instance()` singleton in `execute_forget_memory_tool_call()` |
| `memory_tool.c` | 332 | Calls `metadata_store_get_instance()` singleton in `execute_recall_memories_tool_call()` |
| `vector_db_tool.c` | 4 | `#include "db/document_store.h"` - direct low-level DB include |
| `subagent_tool.c` | 7 | `#include "../session/conversation_tracker.h"` - unused dead include |
| `todo_display.c` | 2 | `#include "../ui/terminal.h"` - Tools layer depending on UI layer |

**Root cause:** Tools bypass Services DI to access metadata_store singleton directly. todo_display has an upward dependency on UI.

---

### `lib/session/` - 2 violations

| File | Line | Violation |
|------|------|-----------|
| `rolling_summary.c` | 3, 6 | `#include "../network/http_client.h"` and `#include "../network/streaming.h"` |
| `rolling_summary.c` | 251 | Calls `http_post_with_headers()` - session layer making direct HTTP calls |
| `conversation_tracker.c` | 8 | `#include "../llm/embeddings_service.h"` - unused dead include |

**Root cause:** rolling_summary.c makes direct HTTP calls instead of going through provider abstraction.

---

### `lib/llm/` - 6 CRITICAL (header-level coupling)

| File | Line | Violation |
|------|------|-----------|
| `embeddings_service.h` | 5 | `#include "db/vector_db.h"` - LLM layer depending on DB layer |
| `llm_provider.h` | 4 | `#include "../session/conversation_tracker.h"` - LLM depending on Session |
| `llm_provider.h` | 5 | `#include "../tools/tools_system.h"` - LLM depending on Tools |
| `llm_provider.h` | 7 | `#include "../ui/output_formatter.h"` - LLM depending on UI |
| `model_capabilities.h` | 6 | `#include "../ui/output_formatter.h"` - LLM depending on UI |
| `model_capabilities.h` | 7 | `#include "../tools/tools_system.h"` - LLM depending on Tools |

**Root cause:** Core LLM abstraction headers bake in dependencies on Session, Tools, and UI types (`ConversationHistory`, `ToolRegistry`, `ParsedResponse`). These cascade everywhere the headers are included. Fixing requires forward declarations or extracting shared types.

---

### `lib/network/` - 3 CRITICAL (upward dependencies)

| File | Line | Violation |
|------|------|-----------|
| `api_common.h` | 4 | `#include "../session/conversation_tracker.h"` - Network depending on Session |
| `api_common.h` | 5 | `#include "../tools/tools_system.h"` - Network depending on Tools |
| `api_common.c` | 3 | `#include "../llm/model_capabilities.h"` - Network depending on LLM |

**Root cause:** `api_common` builds JSON payloads using Session and Tools types. This module is misplaced in the network layer - it belongs in a higher layer or needs dependency inversion.

---

### `lib/policy/` - 3 violations

| File | Line | Violation |
|------|------|-----------|
| `approval_gate.c` | 18-19, 573-778 | Includes and calls `tool_extension.h` and `subagent_tool.h` from Tools layer |
| `subagent_approval.c` | 16-17 | Includes `output_formatter.h` (UI) and `subagent_tool.h` (Tools) |
| `gate_prompter.c` | 2 | `#include "../ui/terminal.h"` - Policy depending on UI |

**Root cause:** Policy queries tool metadata directly instead of tools registering their gate categories. Terminal formatting is pulled from UI instead of being injected.

---

### `lib/ui/` - 8 violations

| File | Line | Violation |
|------|------|-----------|
| `repl.h` | 10 | `#include "../agent/session.h"` - UI depending on Agent |
| `repl.c` | 9 | `#include "../agent/recap.h"` - UI depending on Agent |
| `repl.c` | 15 | `#include "../tools/subagent_tool.h"` - UI depending on Tools |
| `repl.c` | 16 | `#include "../ipc/message_poller.h"` - UI depending on IPC |
| `repl.c` | 17 | `#include "../ipc/notification_formatter.h"` - UI depending on IPC |
| `output_formatter.c` | 6 | `#include "../tools/todo_display.h"` - UI depending on Tools |
| `memory_commands.c` | 3 | `#include "db/metadata_store.h"` - direct low-level DB include |
| `memory_commands.c` | 324-414 | Calls `vector_db_service_get_database()` then low-level `vector_db_*()` functions directly, bypassing service |

**Root cause:** `repl.c` is a "god module" that reaches into agent, tools, and IPC. `memory_commands.c` unwraps the service to call low-level vector DB functions.

---

### `lib/ipc/` - 1 violation

| File | Line | Violation |
|------|------|-----------|
| `notification_formatter.c` | 3, 46, 49 | `#include "../ui/terminal.h"`, calls `terminal_strip_ansi()` - IPC depending on UI |

**Root cause:** ANSI stripping is a UI concern that leaked into the IPC layer.

---

### `lib/util/` - 2 violations

| File | Line | Violation |
|------|------|-----------|
| `context_retriever.c` | 3, 89 | `#include "../db/metadata_store.h"`, calls `metadata_store_get_instance()` singleton directly |
| `prompt_loader.c` | 2, 312 | `#include "../tools/tool_extension.h"`, calls `tool_extension_get_tools_description()` - Utils depending upward on Tools |

**Root cause:** Utils layer reaching upward into Tools and directly accessing DB singletons.

---

## Summary

| Category | Count |
|----------|-------|
| Direct HTTP calls bypassing provider abstraction | 4 locations |
| Direct low-level DB access (document_store, metadata_store, vector_db) | 8 locations |
| Direct singleton calls bypassing Services DI | 6 locations |
| Upward dependencies (lower layer including higher) | 15 locations |
| Unused/dead includes | 2 locations |
| **Total distinct violations** | **~47** |

## Systemic Issues

### 1. Header-level type coupling (`llm_provider.h`, `model_capabilities.h`, `api_common.h`)
These headers bake in dependencies on `ConversationHistory`, `ToolRegistry`, and `ParsedResponse` types. Every file that includes them transitively depends on Session, Tools, and UI. Fix requires extracting shared types to `lib/types.h` or using forward declarations.

### 2. Direct HTTP calls from non-network code
`session.c`, `tool_executor.c`, `recap.c`, and `rolling_summary.c` all call `http_post_with_headers()` directly. These should go through the LLM provider abstraction.

### 3. metadata_store singleton access
`memory_tool.c`, `context_retriever.c`, and `memory_commands.c` all call `metadata_store_get_instance()` directly. metadata_store should be accessible through Services DI.

### 4. `api_common` is misplaced
`lib/network/api_common.c` builds JSON payloads using Session and Tools types. It belongs in a higher layer (e.g., `lib/llm/` or a new `lib/api/` module).

### 5. `repl.c` is a god module
It reaches into Agent, Tools, and IPC layers. Needs refactoring to receive callbacks/interfaces instead of importing concrete implementations.
