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

## Session 8: Extract api_round_trip.c

**Goal**: Extract LLM communication logic from `tool_executor.c`.

### New File: lib/agent/api_round_trip.h

```c
#ifndef LIB_AGENT_API_ROUND_TRIP_H
#define LIB_AGENT_API_ROUND_TRIP_H

#include "session.h"
#include "../types.h"

typedef struct {
    char* content;
    ToolCall* tool_calls;
    int tool_call_count;
    int needs_continuation;
} LLMRoundTripResult;

int api_round_trip_execute(AgentSession* session,
                           const char* user_message,
                           int max_tokens,
                           const char** headers,
                           LLMRoundTripResult* result);

void api_round_trip_cleanup(LLMRoundTripResult* result);

#endif
```

### New File: lib/agent/api_round_trip.c

Extract from `tool_executor.c`:
- Payload building (calls to `session_build_*_json_payload`)
- HTTP POST execution (lines 308-357)
- Response parsing (lines 351-410)
- Tool call extraction

### Update mk/lib.mk

Add `lib/agent/api_round_trip.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "what is 2+2"
```

---

## Session 9: Extract tool_batch_executor.c

**Goal**: Extract single-batch tool execution from `tool_executor.c`.

### New File: lib/agent/tool_batch_executor.h

```c
#ifndef LIB_AGENT_TOOL_BATCH_EXECUTOR_H
#define LIB_AGENT_TOOL_BATCH_EXECUTOR_H

#include "session.h"
#include "tool_orchestration.h"
#include "../types.h"

typedef struct {
    AgentSession* session;
    ToolOrchestrationContext* orchestration;
    int allow_subagent;
} ToolBatchContext;

int tool_batch_execute(ToolBatchContext* ctx,
                       ToolCall* calls,
                       int call_count,
                       ToolResult* results,
                       int* executed_count);

#endif
```

### New File: lib/agent/tool_batch_executor.c

Extract from `tool_executor.c`:
- Tool iteration loop
- Approval checking (via tool_orchestration)
- Tool execution (native or MCP)
- Interrupt handling
- Result logging

### Update mk/lib.mk

Add `lib/agent/tool_batch_executor.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "read the file README.md"
```

---

## Session 10: Extract iterative_loop.c and Finalize tool_executor.c

**Goal**: Extract main agentic loop and slim `tool_executor.c` to thin entry point.

### New File: lib/agent/iterative_loop.h

```c
#ifndef LIB_AGENT_ITERATIVE_LOOP_H
#define LIB_AGENT_ITERATIVE_LOOP_H

#include "session.h"
#include "../types.h"

int iterative_loop_run(AgentSession* session,
                       const char* user_message,
                       int max_tokens,
                       const char** headers,
                       ToolResult* initial_results,
                       int initial_count);

#endif
```

### New File: lib/agent/iterative_loop.c

Extract from `tool_executor.c`:
- Main `while(1)` loop (lines 262-676)
- Token management per iteration
- Termination condition checking

### Update lib/agent/tool_executor.c

Slim to ~50 line entry point that:
1. Creates contexts
2. Calls `tool_batch_execute()` for initial batch
3. Calls `iterative_loop_run()` for follow-ups

### Update mk/lib.mk

Add `lib/agent/iterative_loop.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "create a file called test.txt with hello world, then read it back"
```

---

## Session 11: Extract session_configurator.c

**Goal**: Extract configuration loading from `session.c`.

### New File: lib/agent/session_configurator.h

```c
#ifndef LIB_AGENT_SESSION_CONFIGURATOR_H
#define LIB_AGENT_SESSION_CONFIGURATOR_H

#include "session.h"
#include "../services/services.h"

int session_configurator_load(AgentSession* session, Services* services);
APIType session_configurator_detect_api_type(const char* api_url);

#endif
```

### New File: lib/agent/session_configurator.c

Extract from `session.c` lines 231-297:
- API settings loading
- API type detection from URL
- Embeddings service initialization
- System prompt loading

### Update lib/agent/session.c

Replace extracted code with call to `session_configurator_load()`.

### Update mk/lib.mk

Add `lib/agent/session_configurator.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
```

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
- [ ] `lib/agent/api_round_trip.h`
- [ ] `lib/agent/api_round_trip.c`
- [ ] `lib/agent/tool_batch_executor.h`
- [ ] `lib/agent/tool_batch_executor.c`
- [ ] `lib/agent/iterative_loop.h`
- [ ] `lib/agent/iterative_loop.c`
- [ ] `lib/agent/session_configurator.h`
- [ ] `lib/agent/session_configurator.c`
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
