# Plan: Fix Architectural Violations

Fix ~47 architectural violations identified in `REARCHITECTURE.md`, organized into 8 phases ordered by risk (low-to-high).

---

## Phase 1: Remove Dead Includes (2 violations, zero risk) ✅ COMPLETE

Pure deletions, no behavioral change.

| File | Line | Remove |
|------|------|--------|
| `lib/tools/subagent_tool.c` | 7 | `#include "../session/conversation_tracker.h"` (unused) |
| `lib/session/conversation_tracker.c` | 8 | `#include "../llm/embeddings_service.h"` (unused) |
| `lib/ui/output_formatter.c` | 6 | `#include "../tools/todo_display.h"` (unused, verified: no `todo_display_` calls in file) |

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh`

---

## Phase 2: Add metadata_store to Services DI (7+ call sites) ✅ COMPLETE

metadata_store is the only DB singleton not in the DI container. All callers use `metadata_store_get_instance()` directly.

### 2a: Extend Services container

- **`lib/services/services.h`**: Add `metadata_store_t* metadata_store` field + `#include "db/metadata_store.h"` + accessor decl
- **`lib/services/services.c`**: Init in `services_create_default()` (after `document_store_set_services`), NULL in `services_create_empty()`, destroy in `services_destroy()`, add accessor function
- **`test/stubs/services_stub.c`**: Add NULL init + stub accessor

### 2b: Route callers through DI

All these files already have a `g_services` or `Services*` pointer:

| File | Lines | Change |
|------|-------|--------|
| `lib/tools/memory_tool.c` | 154, 230, 332 | `metadata_store_get_instance()` -> `services_get_metadata_store(g_services)` |
| `lib/util/context_retriever.c` | 89 | Same replacement |
| `lib/ui/memory_commands.c` | 117, 149, 191, 251, 414, 491 | Same replacement |
| `lib/db/document_store.c` | 110 | Same replacement (in `document_store_create()`, `g_services` is set before this is called) |

Remove direct `#include "db/metadata_store.h"` from files that get it transitively via services.h.

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh` (specifically test_memory_tool, test_document_store, test_vector_db_tool)

---

## Phase 3: Decouple document_store from Embeddings (1 CRITICAL)

`lib/db/document_store.c` includes `llm/embeddings_service.h` and calls `embeddings_service_get_vector()` directly (lines 321-341, 406-419). DB layer should not depend on LLM layer.

### Approach: Move `_text` convenience functions to document_store wrapper layer

**Callers** (verified):
- `lib/session/conversation_tracker.c:363` - `document_store_add_text()`
- `lib/session/conversation_tracker.c:474` - `document_store_search_text()`
- `lib/tools/vector_db_tool.c:628,709,824` - `document_store_add_text()`
- `lib/tools/vector_db_tool.c:877` - `document_store_search_text()`

**Changes:**
1. **`lib/db/vector_db_service.h`**: Add `vector_db_service_add_text()` and `vector_db_service_search_text()` that accept a `document_store_t*` + Services-aware embedding
2. **`lib/db/vector_db_service.c`**: Implement by: get embeddings from Services -> compute vector -> delegate to `document_store_add()`/`document_store_search()`
3. **`lib/db/document_store.c`**: Remove `#include "llm/embeddings_service.h"` and delete `document_store_add_text()`/`document_store_search_text()` bodies
4. **`lib/db/document_store.h`**: Remove `_text` function declarations
5. **Update callers**: conversation_tracker.c and vector_db_tool.c call the new `vector_db_service_*` variants (both have access to Services)

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh` (test_document_store, test_vector_db_tool, test_conversation_tracker, test_conversation_vector_db)

---

## Phase 4: Header-Level Type Coupling (9 CRITICAL violations)

Three headers bake in cross-layer type dependencies via `#include`. Since all uses are through pointers in function signatures, forward declarations or type extraction can break the chains.

### Problem types and their homes

| Type | Defined In | Used By (as pointer) |
|------|-----------|---------------------|
| `ConversationHistory` | `conversation_tracker.h` | `llm_provider.h`, `api_common.h` |
| `ToolRegistry` | `tools_system.h` | `llm_provider.h`, `model_capabilities.h`, `api_common.h` |
| `ParsedResponse` | `output_formatter.h` | `llm_provider.h`, `model_capabilities.h` |

### Approach: Forward declarations + named struct tags

1. **Give anonymous structs named tags** (enables forward declaration):
   - `lib/tools/tools_system.h`: `typedef struct ToolRegistry { ... } ToolRegistry;`
   - `lib/ui/output_formatter.h`: `typedef struct ParsedResponse { ... } ParsedResponse;`

2. **ConversationHistory** is a DARRAY typedef - forward-declare the underlying struct:
   - `DARRAY_DECLARE(ConversationHistory, ConversationMessage)` creates `ConversationHistory_darray_t`
   - Add `typedef struct ConversationHistory_darray_t ConversationHistory;` as forward decl where needed

3. **Fix the three culprit headers:**

   **`lib/llm/llm_provider.h`**: Remove includes of `conversation_tracker.h`, `tools_system.h`, `output_formatter.h`. Add forward declarations for `ConversationHistory`, `ToolRegistry`, `ParsedResponse`.

   **`lib/llm/model_capabilities.h`**: Remove includes of `output_formatter.h`, `tools_system.h`. Add forward declarations.

   **`lib/network/api_common.h`**: Remove includes of `conversation_tracker.h`, `tools_system.h`. Add forward declarations.

4. **`lib/llm/embeddings_service.h`**: Remove `#include "db/vector_db.h"`. Forward-declare `vector_t` or move its definition to `lib/types.h`.

5. **Fix cascading compilation errors**: Any `.c` file that was relying on transitive includes will need explicit `#include` for the types it uses. Do this incrementally - change one header, build, fix errors, repeat.

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh` (all tests - highest risk phase)

---

## Phase 5: Extract ANSI Constants (5 terminal.h violations)

Several non-UI modules include `terminal.h` for TERM_* string constants.

### Changes

1. **New file `lib/util/ansi_codes.h`**: Extract all `TERM_RESET`, `TERM_BOLD`, `TERM_CYAN`, etc. `#define` constants (zero-dependency header)
2. **`lib/ui/terminal.h`**: Replace constant definitions with `#include "../util/ansi_codes.h"`
3. **Update non-UI files** to include `ansi_codes.h` instead of `terminal.h`:
   - `lib/tools/todo_display.c` (if only using constants)
   - `lib/policy/gate_prompter.c` (if only using constants; if using `terminal_*()` functions, keep terminal.h - gate prompter IS UI)
4. **`lib/ipc/notification_formatter.c`**: Uses `terminal_strip_ansi()`. Move that function to `lib/util/common_utils.c` (it's a pure string transform, not terminal-specific)
5. **`lib/ui/output_formatter.h`**: Move `#include "terminal.h"` from header to `.c` file (header doesn't use TERM_* macros)

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh`

---

## Phase 6: Upward Dependencies (8-10 violations)

### 6a: Policy -> Tools (dependency inversion)

`lib/policy/approval_gate.c` and `subagent_approval.c` include `tool_extension.h` and `subagent_tool.h`.

**Fix**: Define a `tool_gate_callbacks_t` struct in `lib/policy/approval_gate.h` with function pointers for `is_extension_tool`, `get_gate_category`, `get_match_arg`, `get_approval_channel`. Wire at agent init by pointing to actual tool functions.

### 6b: Util -> Tools

`lib/util/prompt_loader.c` includes `tool_extension.h` to call `tool_extension_get_tools_description()`.

**Fix**: Change prompt_loader to accept tools description as a parameter. Caller (session.c) provides it.

### 6c: UI -> low-level DB

`lib/ui/memory_commands.c` calls `vector_db_service_get_database()` then low-level `vector_db_*()` functions.

**Fix**: Add service-level wrappers to `vector_db_service.h`: `_list_indices()`, `_get_index_size()`, `_get_index_capacity()`, `_has_index()`.

### 6d: REPL god module

`lib/ui/repl.c` includes agent, tools, IPC modules.

**Fix**: Accept as-is OR move `repl.c/h` from `lib/ui/` to `lib/agent/` since it's agent-level orchestration code. Update include paths and `mk/sources.mk`.

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh`

---

## Phase 7: Direct HTTP Calls (4 violations, HIGH risk)

`session.c`, `tool_executor.c`, `recap.c`, `rolling_summary.c` all call `http_post_with_headers()` / `http_post_streaming()` directly.

### Approach: LLM client abstraction

**New files: `lib/llm/llm_client.h` and `llm_client.c`**

```c
typedef struct LLMClient { ... } LLMClient;
int llm_client_send(LLMClient* client, const char* payload, HTTPResponse* response);
int llm_client_send_streaming(LLMClient* client, const char* payload, StreamingHTTPConfig* config);
```

Encapsulates: header construction + HTTP call. Does NOT build payloads or parse responses.

**Update callers** to use `llm_client_send()` instead of raw HTTP functions.

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh` + manual testing with `--debug`

---

## Phase 8: Agent Layer Cleanup (3 violations)

After earlier phases, remaining cleanup:

- `lib/agent/agent.c:11`: Remove `#include "../db/document_store.h"` (call to `document_store_set_services()` should already be in `services_create_default()`)
- `lib/agent/session.c:45-46`: Remove direct includes of `task_store.h` and `embeddings_service.h` if accessed only through Services
- `lib/agent/tool_executor.c:395`: Route `get_model_registry()` through session instead of global singleton

**Verify:** `./scripts/build.sh && ./scripts/run_tests.sh`

---

## Phase Dependencies

```
Phase 1 (dead includes)         -- independent
Phase 2 (metadata_store DI)     -- independent
Phase 3 (document_store)        -- after Phase 2
Phase 4 (header coupling)       -- independent, do before 5-8
Phase 5 (ANSI extraction)       -- independent
Phase 6 (upward deps)           -- after Phases 4, 5
Phase 7 (HTTP abstraction)      -- after Phase 4
Phase 8 (agent cleanup)         -- after Phases 2, 3, 7
```

**Recommended order: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8**

## Risk Summary

| Phase | Risk | Files | Description |
|-------|------|-------|-------------|
| 1 | Zero | 3 | Delete dead includes |
| 2 | Low | ~8 | metadata_store DI routing |
| 3 | Medium | ~6 | Move _text functions |
| 4 | **High** | 10-15 | Header decoupling (cascading) |
| 5 | Low | ~6 | Extract ANSI constants |
| 6 | Medium | 8-12 | Dependency inversion |
| 7 | **High** | 5-7 | New LLM client abstraction |
| 8 | Low | 2-3 | Final cleanup |
