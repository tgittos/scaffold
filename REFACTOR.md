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

## Session 2: Convert Tool Singleton Calls

**Goal**: Replace singleton calls in tool modules with Services accessors.

### Files to Modify

#### 2.1 lib/tools/todo_tool.h

Update function signature:

```c
// Change from:
int register_todo_tool(ToolRegistry* registry, TodoList* todo_list);

// Change to:
int register_todo_tool(ToolRegistry* registry, TodoList* todo_list, Services* services);
```

#### 2.2 lib/tools/todo_tool.c

Add static Services pointer and use it:

```c
#include "../services/services.h"

static Services* g_services = NULL;

int register_todo_tool(ToolRegistry* registry, TodoList* todo_list, Services* services) {
    g_services = services;  // Store services
    // ... rest of existing code
}
```

Replace all `task_store_get_instance()` calls with `services_get_task_store(g_services)`:
- Line 19: in `sync_todolist_from_store()`
- Line 148: in `execute_todo_tool_call()`

#### 2.3 lib/tools/messaging_tool.h

Add Services setter:

```c
void messaging_tool_set_services(Services* services);
```

#### 2.4 lib/tools/messaging_tool.c

Add static Services pointer:

```c
#include "../services/services.h"

static Services* g_services = NULL;

void messaging_tool_set_services(Services* services) {
    g_services = services;
}
```

Replace all `message_store_get_instance()` calls with `services_get_message_store(g_services)`:
- Line 139
- Line 200
- Line 319
- Line 428
- Line 491

#### 2.5 lib/agent/session.c

Update tool registration call (in `session_init()`):

```c
// Change from:
register_todo_tool(&session->tools, &session->todo_list);

// Change to:
register_todo_tool(&session->tools, &session->todo_list, session->services);
```

Add call to set messaging services:

```c
messaging_tool_set_services(session->services);
```

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "create a todo called test"
./ralph "list my todos"
```

---

## Session 3: Convert IPC Singleton Calls

**Goal**: Replace singleton calls in IPC modules with Services accessors.

### Files to Modify

#### 3.1 lib/ipc/message_poller.h

Update create function:

```c
// Change from:
message_poller_t* message_poller_create(const char* agent_id, int poll_interval_ms);

// Change to:
message_poller_t* message_poller_create(const char* agent_id, int poll_interval_ms, Services* services);
```

#### 3.2 lib/ipc/message_poller.c

Add Services to struct:

```c
struct message_poller {
    // ... existing fields
    Services* services;  // ADD THIS
};
```

Update create function and store services:

```c
message_poller_t* message_poller_create(const char* agent_id, int poll_interval_ms, Services* services) {
    message_poller_t* poller = calloc(1, sizeof(message_poller_t));
    // ... existing init code
    poller->services = services;
    return poller;
}
```

Replace `message_store_get_instance()` with `services_get_message_store(poller->services)` at line 39.

#### 3.3 lib/ipc/notification_formatter.h

Update function signature:

```c
// Change from:
notification_bundle_t* notification_bundle_create(const char* agent_id);

// Change to:
notification_bundle_t* notification_bundle_create(const char* agent_id, Services* services);
```

#### 3.4 lib/ipc/notification_formatter.c

Add Services parameter and use it:

```c
notification_bundle_t* notification_bundle_create(const char* agent_id, Services* services) {
    // Replace message_store_get_instance() at line 148 with:
    message_store_t* store = services_get_message_store(services);
    // ... rest of existing code
}
```

#### 3.5 lib/agent/session.c

Update message poller creation (in `session_start_message_polling()`):

```c
// Change from:
session->message_poller = message_poller_create(session->session_id, session->polling_config.poll_interval_ms);

// Change to:
session->message_poller = message_poller_create(session->session_id, session->polling_config.poll_interval_ms, session->services);
```

#### 3.6 lib/agent/async_executor.c

Update notification bundle creation:

```c
// Find call to notification_bundle_create() and add services parameter
notification_bundle_create(agent_id, services);
```

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
```

---

## Session 4: Convert Remaining Singleton Calls

**Goal**: Replace all remaining singleton calls with Services accessors.

### Files to Modify

#### 4.1 lib/agent/session.c

Replace direct singleton calls at lines 102, 106, 313:

```c
// Change from:
if (task_store_get_instance() == NULL) { ... }
if (message_store_get_instance() == NULL) { ... }

// Change to:
if (services_get_task_store(session->services) == NULL) { ... }
if (services_get_message_store(session->services) == NULL) { ... }
```

#### 4.2 lib/tools/subagent_process.h

Update function signatures:

```c
// Change from:
void cleanup_subagent(Subagent *sub);
void subagent_notify_parent(const Subagent* sub);

// Change to:
void cleanup_subagent(Subagent *sub, Services* services);
void subagent_notify_parent(const Subagent* sub, Services* services);
```

#### 4.3 lib/tools/subagent_process.c

Add Services parameter and use it at lines 126, 286:

```c
void cleanup_subagent(Subagent *sub, Services* services) {
    // Replace message_store_get_instance() with:
    message_store_t* store = services_get_message_store(services);
    // ... rest of code
}
```

#### 4.4 lib/tools/subagent_tool.c

Update calls to pass services:

```c
cleanup_subagent(sub, registry->services);
subagent_notify_parent(sub, registry->services);
```

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "what files are in this directory"  # Tests subagent tool
```

---

## Session 5: Remove Singleton Getters

**Goal**: Delete all `*_get_instance()` functions and remove fallback from Services accessors.

### Files to Modify

#### 5.1 lib/ipc/message_store.h

Remove declaration:

```c
// DELETE THIS LINE:
message_store_t* message_store_get_instance(void);
```

#### 5.2 lib/ipc/message_store.c

Remove implementation (around lines 180-183):

```c
// DELETE THIS FUNCTION:
message_store_t* message_store_get_instance(void) {
    pthread_once(&g_store_once, create_store_instance);
    return g_store_instance;
}
```

#### 5.3 lib/db/task_store.h

Remove declaration:

```c
// DELETE THIS LINE:
task_store_t* task_store_get_instance(void);
```

#### 5.4 lib/db/task_store.c

Remove implementation (around lines 125-128).

#### 5.5 lib/db/vector_db_service.h

Remove declaration:

```c
// DELETE THIS LINE:
vector_db_service_t* vector_db_service_get_instance(void);
```

#### 5.6 lib/db/vector_db_service.c

Remove implementation (around lines 36-39).

#### 5.7 lib/llm/embeddings_service.h

Remove declaration:

```c
// DELETE THIS LINE:
embeddings_service_t* embeddings_service_get_instance(void);
```

#### 5.8 lib/llm/embeddings_service.c

Remove implementation (around lines 44-46).

#### 5.9 lib/services/services.c

Remove fallback from accessors. Change:

```c
message_store_t* services_get_message_store(Services* services) {
    if (services != NULL && services->message_store != NULL) {
        return services->message_store;
    }
    return message_store_get_instance();  // DELETE THIS FALLBACK
}
```

To:

```c
message_store_t* services_get_message_store(Services* services) {
    return (services != NULL) ? services->message_store : NULL;
}
```

Apply same change to all four accessors.

### Verification

```bash
./scripts/build.sh  # Should compile with no undefined references
./scripts/run_tests.sh
./ralph "hello"
```

---

## Session 6: Extract tool_orchestration.c

**Goal**: Extract approval and deduplication logic from `tool_executor.c`.

### New File: lib/agent/tool_orchestration.h

```c
#ifndef LIB_AGENT_TOOL_ORCHESTRATION_H
#define LIB_AGENT_TOOL_ORCHESTRATION_H

#include "../services/services.h"
#include "../policy/approval_gate.h"
#include "../types.h"
#include "../util/ptrarray.h"

typedef struct {
    Services* services;
    ApprovalGateConfig* gate_config;
    StringArray* executed_tracker;
    int subagent_spawned;
} ToolOrchestrationContext;

void tool_orchestration_init(ToolOrchestrationContext* ctx,
                             Services* services,
                             ApprovalGateConfig* gate_config);
void tool_orchestration_cleanup(ToolOrchestrationContext* ctx);

int tool_orchestration_check_approval(ToolOrchestrationContext* ctx,
                                      const ToolCall* call,
                                      ToolResult* result);

int tool_orchestration_is_duplicate(ToolOrchestrationContext* ctx,
                                    const char* tool_call_id);

void tool_orchestration_mark_executed(ToolOrchestrationContext* ctx,
                                      const char* tool_call_id);

int tool_orchestration_can_spawn_subagent(ToolOrchestrationContext* ctx,
                                          const char* tool_name);

#endif
```

### New File: lib/agent/tool_orchestration.c

Extract from `tool_executor.c`:
- `is_file_write_tool()` (lines 25-30)
- `is_file_tool()` (lines 32-38)
- `check_tool_approval()` (lines 44-138)
- `is_tool_already_executed()` (lines 235-242)
- `add_executed_tool()` (lines 244-260)
- Subagent duplicate prevention logic

### Update lib/agent/tool_executor.c

Replace extracted functions with calls to `tool_orchestration_*()`.

### Update mk/lib.mk

Add `lib/agent/tool_orchestration.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
./ralph "write hello to test.txt"  # Tests approval
```

---

## Session 7: Extract conversation_state.c

**Goal**: Extract conversation history management from `tool_executor.c`.

### New File: lib/agent/conversation_state.h

```c
#ifndef LIB_AGENT_CONVERSATION_STATE_H
#define LIB_AGENT_CONVERSATION_STATE_H

#include "session.h"
#include "../types.h"

char* conversation_build_assistant_tool_message(const ToolCall* calls,
                                                int count,
                                                const char* content);

int conversation_append_assistant(AgentSession* session,
                                  const char* content,
                                  const ToolCall* calls,
                                  int count);

int conversation_append_tool_results(AgentSession* session,
                                     const ToolResult* results,
                                     int count);

#endif
```

### New File: lib/agent/conversation_state.c

Extract from `tool_executor.c`:
- `construct_openai_assistant_message_with_tools()` (lines 140-233)
- Tool result appending logic (lines 435-458, 649-670, 801-805)

### Update mk/lib.mk

Add `lib/agent/conversation_state.c` to `LIB_AGENT_SRC`.

### Verification

```bash
./scripts/build.sh && ./scripts/run_tests.sh
```

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
- [ ] `lib/agent/tool_orchestration.h`
- [ ] `lib/agent/tool_orchestration.c`
- [ ] `lib/agent/conversation_state.h`
- [ ] `lib/agent/conversation_state.c`
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
- [ ] `lib/agent/tool_executor.c` - Slim to entry point
- [ ] `lib/tools/tools_system.h` - Add Services* to ToolRegistry
- [ ] `lib/tools/tools_system.c` - Initialize Services
- [ ] `lib/tools/todo_tool.h` - Add Services parameter
- [ ] `lib/tools/todo_tool.c` - Use Services accessor
- [ ] `lib/tools/messaging_tool.h` - Add Services setter
- [ ] `lib/tools/messaging_tool.c` - Use Services accessor
- [ ] `lib/tools/subagent_process.h` - Add Services parameter
- [ ] `lib/tools/subagent_process.c` - Use Services accessor
- [ ] `lib/tools/subagent_tool.c` - Pass Services
- [ ] `lib/ipc/message_poller.h` - Add Services to create
- [ ] `lib/ipc/message_poller.c` - Store and use Services
- [ ] `lib/ipc/notification_formatter.h` - Add Services parameter
- [ ] `lib/ipc/notification_formatter.c` - Use Services accessor
- [ ] `lib/services/services.c` - Remove fallback
- [ ] `mk/lib.mk` - Add new source files

### Deleted Code
- [ ] `message_store_get_instance()` from message_store.c/h
- [ ] `task_store_get_instance()` from task_store.c/h
- [ ] `vector_db_service_get_instance()` from vector_db_service.c/h
- [ ] `embeddings_service_get_instance()` from embeddings_service.c/h
- [ ] Singleton fallback from `services_get_*()` functions
