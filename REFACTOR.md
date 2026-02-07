# Refactor Plan: Services DI + God Module Breakup

This document describes a multi-session refactoring effort to:
1. Properly use the Services dependency injection container
2. Break up the `tool_executor.c` God module (823 lines)
3. Break up the `session.c` God module (752 lines)

Each session is designed to be completable independently, with clear goals and verification steps.

---

## Background

### Current Problems

**Services Container Unused**: The `Services` container exists at `lib/services/services.c` but all code bypasses it by calling singleton getters directly (`message_store_get_instance()`, `task_store_get_instance()`, etc.). This makes testing with mocks impossible.

**God Modules**: Two files have grown too large with too many responsibilities:
- `lib/agent/tool_executor.c` (823 lines) - handles approval, HTTP, parsing, history, loop control
- `lib/agent/session.c` (752 lines) - handles config, dispatch, parsing, persistence

### Target Architecture

```
lib/agent/
├── session.c              → Thin orchestrator (~300 lines)
├── session_configurator.c → Config loading (NEW)
├── message_dispatcher.c   → Path selection (NEW)
├── message_processor.c    → Response handling (NEW)
├── tool_executor.c        → Thin entry point (~50 lines)
├── tool_orchestration.c   → Approval + dedup (NEW)
├── api_round_trip.c       → LLM communication (NEW)
├── tool_batch_executor.c  → Batch execution (NEW)
├── conversation_state.c   → History management (NEW)
└── iterative_loop.c       → Main agentic loop (NEW)
```

---

## Session 5: Remove Singleton Getters ✅ COMPLETE

**Goal**: Delete all `*_get_instance()` functions and remove fallback from Services accessors.

All singleton getters removed, services accessors cleaned to simple ternary (no fallback).
Architecture violations addressed in separate audit (REARCHITECTURE_PLAN.md, now deleted).

---

## Session 6: Extract tool_orchestration.c ✅ COMPLETE

**Goal**: Extract approval and deduplication logic from `tool_executor.c`.

Extracted `is_file_write_tool`, `is_file_tool`, `check_tool_approval`, `is_tool_already_executed`,
`add_executed_tool`, and subagent duplicate prevention into `lib/agent/tool_orchestration.c`.

The `ToolOrchestrationContext` owns the dedup tracker and subagent flag. A single context is
created in `tool_executor_run_workflow` and shared with `tool_executor_run_loop`, so dedup
tracking spans the full workflow (initial batch IDs are seeded before entering the loop).

---

## Session 7: Extract conversation_state.c ✅ COMPLETE

**Goal**: Extract conversation history management from `tool_executor.c`.

Extracted three functions into `lib/agent/conversation_state.c`:
- `conversation_build_assistant_tool_message` — renamed from `construct_openai_assistant_message_with_tools`,
  builds OpenAI-format assistant JSON with tool_calls array.
- `conversation_append_assistant` — appends assistant message to conversation history,
  using model registry formatting with fallback to tool-name summary.
- `conversation_append_tool_results` — appends tool results to conversation history,
  supporting both direct 1:1 mapping and indexed mapping via `call_indices` parameter.

`session.c` and `streaming_handler.c` updated to call the renamed builder function.
Full migration of those callers to `conversation_append_assistant` deferred to Sessions 11-13.

---

## Session 8: Extract api_round_trip.c ✅ COMPLETE

**Goal**: Extract LLM communication logic from `tool_executor.c`.

Extracted payload building, HTTP send, response parsing, and tool call extraction into
`lib/agent/api_round_trip.c`. The `LLMRoundTripResult` struct embeds `ParsedResponse` directly
(giving callers access to response_content, thinking_content, and token counts) plus extracted
tool calls. `tool_executor_run_loop` transfers tool call ownership out of the result before
cleanup, since tool calls outlive the parsed response.

---

## Session 9: Extract tool_batch_executor.c ✅ COMPLETE

**Goal**: Extract single-batch tool execution from `tool_executor.c`.

Extracted the duplicated batch execution loops from `tool_executor_run_workflow` (initial batch)
and `tool_executor_run_loop` (follow-up iterations) into a unified `tool_batch_execute` function
in `lib/agent/tool_batch_executor.c`.

The function supports two indexing modes controlled by the `call_indices` parameter:
- **Direct mode** (`call_indices == NULL`): For the initial batch. Direct 1:1 result-to-call
  mapping. All slots are filled on abort/interrupt. No dedup.
- **Compact mode** (`call_indices != NULL`): For loop iterations. Dedup via orchestration
  context. Sparse result mapping with `call_indices`. Only the aborting tool is added on abort;
  remaining non-duplicates get results on interrupt.

Also fixed a pre-existing memory leak where `execute_tool_call` sets `result->tool_call_id`
before failing, and the error handler overwrote it without freeing.

---

## Session 10: Extract iterative_loop.c and Finalize tool_executor.c ✅ COMPLETE

**Goal**: Extract main agentic loop and slim `tool_executor.c` to thin entry point.

Extracted the `while(1)` loop (token management, API round trips, dedup, batch execution,
termination detection) into `lib/agent/iterative_loop.c`. The interface is minimal:
`iterative_loop_run(session, ctx)` — the planned `user_message`/`max_tokens` parameters
were already unused (`(void)`'d) and dropped. `tool_executor.c` is now a 62-line entry
point that initializes orchestration, runs the initial batch, seeds the dedup tracker,
and hands off to `iterative_loop_run`.

---

## Session 11: Extract session_configurator.c ✅ COMPLETE

**Goal**: Extract configuration loading from `session.c`.

Extracted `session_load_config` internals into `lib/agent/session_configurator.c` with two functions:
- `session_configurator_load(AgentSession*)` — config init, API settings copy, embeddings
  reinitialization, system prompt loading, API type detection, context window auto-config.
- `session_configurator_detect_api_type(const char*)` — pure function returning APIType from
  URL string, with NULL safety.

`session_load_config` is now a 3-line delegation wrapper. Unused `ralph_home.h` and
`prompt_loader.h` includes removed from `session.c`. Existing test updated to call
`session_configurator_detect_api_type` directly (including NULL input case).

---

## Session 12: Extract message_dispatcher.c

**Goal**: Extract dispatch path selection from `session.c`.

### New File: lib/agent/message_dispatcher.h

```c
#ifndef LIB_AGENT_MESSAGE_DISPATCHER_H
#define LIB_AGENT_MESSAGE_DISPATCHER_H

#include "session.h"

typedef enum {
    DISPATCH_STREAMING,
    DISPATCH_BUFFERED
} DispatchMode;

DispatchMode message_dispatcher_select_mode(const AgentSession* session);

int message_dispatcher_build_headers(const AgentSession* session,
                                     char*** headers,
                                     int* count);

void message_dispatcher_free_headers(char** headers, int count);

#endif
```

### New File: lib/agent/message_dispatcher.c

Extract from `session.c` lines 518-573:
- Streaming vs buffered decision
- HTTP header construction
- Provider-specific header setup

### Update lib/agent/session.c

Replace extracted code with calls to `message_dispatcher_*()`.

### Update mk/lib.mk

Add `lib/agent/message_dispatcher.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph --debug "hello"  # Check headers in debug output
```

---

## Session 13: Extract message_processor.c and Finalize session.c

**Goal**: Extract response handling and slim `session.c` to orchestrator.

### New File: lib/agent/message_processor.h

```c
#ifndef LIB_AGENT_MESSAGE_PROCESSOR_H
#define LIB_AGENT_MESSAGE_PROCESSOR_H

#include "session.h"

int message_processor_handle_response(AgentSession* session,
                                      const char* response_data,
                                      const char* user_message,
                                      int max_tokens,
                                      const char** headers);

#endif
```

### New File: lib/agent/message_processor.c

Extract from `session.c` lines 597-741:
- Response parsing
- Tool call extraction (model-level vs message-embedded)
- Conversation persistence
- Tool executor dispatch

### Update lib/agent/session.c

Slim to orchestrator (~300 lines) that:
- Handles lifecycle (`session_init`, `session_cleanup`)
- Delegates to `message_dispatcher` and `message_processor`
- Provides public API stubs

### Update mk/lib.mk

Add `lib/agent/message_processor.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "what is the weather"
./ralph "create a todo and list todos"
make check-valgrind
```

---

## Final Verification

After all sessions complete:

```bash
# Full build
./scripts/build.sh

# Full test suite
./scripts/run_tests.sh

# Memory safety
make check-valgrind

# Integration tests
./ralph "hello"
./ralph "create a todo called refactor complete"
./ralph "what files are in the current directory"
./ralph "write hello to test.txt then read it back"
./ralph --debug "what is 2+2"
```

---

## File Checklist

### New Files (16)
- [x] `lib/agent/tool_orchestration.h`
- [x] `lib/agent/tool_orchestration.c`
- [x] `lib/agent/conversation_state.h`
- [x] `lib/agent/conversation_state.c`
- [x] `lib/agent/api_round_trip.h`
- [x] `lib/agent/api_round_trip.c`
- [x] `lib/agent/tool_batch_executor.h`
- [x] `lib/agent/tool_batch_executor.c`
- [x] `lib/agent/iterative_loop.h`
- [x] `lib/agent/iterative_loop.c`
- [x] `lib/agent/session_configurator.h`
- [x] `lib/agent/session_configurator.c`
- [ ] `lib/agent/message_dispatcher.h`
- [ ] `lib/agent/message_dispatcher.c`
- [ ] `lib/agent/message_processor.h`
- [ ] `lib/agent/message_processor.c`

### Modified Files (17)
- [ ] `lib/agent/session.h` - Add Services* field
- [ ] `lib/agent/session.c` - Extract modules, use Services
- [ ] `lib/agent/agent.c` - Connect Services
- [x] `lib/agent/tool_executor.c` - Slim to entry point
- [ ] `lib/tools/tools_system.h` - Add Services* to ToolRegistry
- [ ] `lib/tools/tools_system.c` - Initialize Services
- [ ] `lib/tools/todo_tool.h` - Add Services parameter
- [ ] `lib/tools/todo_tool.c` - Use Services accessor
- [ ] `lib/tools/messaging_tool.h` - Add Services setter
- [ ] `lib/tools/messaging_tool.c` - Use Services accessor
- [x] `lib/tools/subagent_process.h` - Add Services parameter
- [x] `lib/tools/subagent_process.c` - Use Services accessor
- [x] `lib/tools/subagent_tool.c` - Pass Services
- [ ] `lib/ipc/message_poller.h` - Add Services to create
- [ ] `lib/ipc/message_poller.c` - Store and use Services
- [ ] `lib/ipc/notification_formatter.h` - Add Services parameter
- [ ] `lib/ipc/notification_formatter.c` - Use Services accessor
- [x] `lib/services/services.c` - Remove fallback
- [x] `mk/lib.mk` - Add new source files

### Deleted Code
- [x] `message_store_get_instance()` from message_store.c/h
- [x] `task_store_get_instance()` from task_store.c/h
- [x] `vector_db_service_get_instance()` from vector_db_service.c/h
- [x] `embeddings_service_get_instance()` from embeddings_service.c/h
- [x] Singleton fallback from `services_get_*()` functions
