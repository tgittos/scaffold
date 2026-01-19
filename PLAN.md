# Subagent Tool Implementation Plan

## Overview

This plan implements the `subagent` and `subagent_status` tools as specified in SPEC.md (sections 2.3.7, 5.5). These tools enable Ralph to spawn background subprocesses that handle delegated tasks with isolated contexts.

## Key Requirements (from SPEC.md)

1. **`subagent` tool**: Spawn background subprocess with task description
   - Parameters: `task` (required), `context` (optional)
   - Returns: `{subagent_id, status, result}`

2. **`subagent_status` tool**: Query status of running subagent
   - Parameters: `subagent_id` (required), `wait` (optional, default false)
   - Returns: `{subagent_id, status, progress, result, error}`

3. **Configuration**:
   - `max_subagents`: Maximum concurrent subagents (default: 5)
   - `subagent_timeout`: Execution timeout in seconds (default: 300)

4. **Security/Isolation**:
   - Fresh conversation context (no parent history inheritance)
   - No nesting (subagents cannot spawn subagents)
   - Tool restrictions: `subagent` and `subagent_status` disabled in subagent processes

---

## Architecture

### Process Model

```
┌─────────────────────────────────────────────────────────────┐
│                     Parent Ralph Process                      │
│                                                               │
│  ┌────────────────┐                                          │
│  │ SubagentManager │ ─────────────────────────────────────┐  │
│  │                 │                                       │  │
│  │ subagents[]     │                                       │  │
│  │  - id           │                                       │  │
│  │  - pid          │                                       │  │
│  │  - status       │                                       │  │
│  │  - stdout_pipe  │                                       │  │
│  │  - output       │                                       │  │
│  │  - start_time   │                                       │  │
│  └────────────────┘                                       │  │
│           │                                                │  │
│           │ fork()                                         │  │
│           ▼                                                │  │
│  ┌─────────────────┐  ┌─────────────────┐                 │  │
│  │ Subagent Process │  │ Subagent Process │  ...           │  │
│  │ (--subagent)     │  │ (--subagent)     │                │  │
│  │                  │  │                  │                │  │
│  │ - Fresh context  │  │ - Fresh context  │                │  │
│  │ - No subagent    │  │ - No subagent    │                │  │
│  │   tools          │  │   tools          │                │  │
│  │ - Writes result  │  │ - Writes result  │                │  │
│  │   to stdout      │  │   to stdout      │                │  │
│  └────────────────┘  └─────────────────┘                 │  │
└─────────────────────────────────────────────────────────────┘
```

### Communication Flow

1. Parent calls `subagent` tool with task
2. SubagentManager forks new process with `--subagent` flag
3. Child process runs ralph with isolated context, executes task
4. Child writes JSON result to stdout on completion
5. Parent reads result via pipe, updates SubagentManager state
6. `subagent_status` queries SubagentManager for current state

---

## Implementation Steps

### Phase 1: Infrastructure (Files to Create/Modify)

#### 1.1 Create `src/tools/subagent_tool.h`

```c
#ifndef SUBAGENT_TOOL_H
#define SUBAGENT_TOOL_H

#include "tools_system.h"
#include <sys/types.h>
#include <time.h>

// Configuration defaults
#define SUBAGENT_MAX_DEFAULT 5
#define SUBAGENT_TIMEOUT_DEFAULT 300
#define SUBAGENT_ID_LENGTH 16

// Subagent status enum
typedef enum {
    SUBAGENT_STATUS_PENDING,
    SUBAGENT_STATUS_RUNNING,
    SUBAGENT_STATUS_COMPLETED,
    SUBAGENT_STATUS_FAILED,
    SUBAGENT_STATUS_TIMEOUT
} SubagentStatus;

// Individual subagent tracking structure
typedef struct {
    char id[SUBAGENT_ID_LENGTH + 1];  // Unique identifier
    pid_t pid;                         // Process ID
    SubagentStatus status;             // Current status
    int stdout_pipe[2];                // Pipe for reading output
    char *task;                        // Task description
    char *context;                     // Optional context
    char *output;                      // Accumulated output
    size_t output_len;                 // Output length
    char *result;                      // Final result (when completed)
    char *error;                       // Error message (when failed)
    time_t start_time;                 // When spawned
} Subagent;

// Manager for all subagents
typedef struct {
    Subagent *subagents;              // Array of subagents
    int count;                         // Current count
    int max_subagents;                 // Maximum allowed
    int timeout_seconds;               // Timeout for each subagent
    int is_subagent_process;           // Flag: running as subagent (disable nesting)
} SubagentManager;

// Manager lifecycle
int subagent_manager_init(SubagentManager *manager);
void subagent_manager_cleanup(SubagentManager *manager);

// Subagent operations
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out);
int subagent_get_status(SubagentManager *manager, const char *subagent_id, int wait,
                        SubagentStatus *status, char **result, char **error);
int subagent_poll_all(SubagentManager *manager);  // Non-blocking poll for completed subagents

// Tool registration
int register_subagent_tool(ToolRegistry *registry, SubagentManager *manager);
int register_subagent_status_tool(ToolRegistry *registry, SubagentManager *manager);

// Execution functions (following existing pattern)
int execute_subagent_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_subagent_status_tool_call(const ToolCall *tool_call, ToolResult *result);

// Subagent mode entry point (called from main when --subagent flag present)
int ralph_run_as_subagent(const char *task, const char *context);

#endif // SUBAGENT_TOOL_H
```

#### 1.2 Create `src/tools/subagent_tool.c`

Key implementation sections:

1. **Subagent Manager**:
   - `subagent_manager_init()`: Initialize from config (max_subagents, timeout)
   - `subagent_manager_cleanup()`: Kill running subagents, free resources

2. **Spawning**:
   - `subagent_spawn()`:
     - Check max_subagents limit
     - Generate unique ID (UUID or random hex)
     - Create pipe for stdout
     - Fork process
     - Child: exec ralph with `--subagent --task "..." [--context "..."]`
     - Parent: store pid, pipe fd, start_time in SubagentManager

3. **Status Checking**:
   - `subagent_get_status()`:
     - If wait=true: block until completion
     - Use `waitpid(WNOHANG)` for non-blocking check
     - Read available output from pipe
     - Check timeout (current_time - start_time > timeout_seconds)
     - Return current status, result, error

4. **Tool Execution**:
   - `execute_subagent_tool_call()`: Parse args, call spawn, return JSON
   - `execute_subagent_status_tool_call()`: Parse args, call get_status, return JSON

5. **Subagent Mode**:
   - `ralph_run_as_subagent()`:
     - Initialize session with `is_subagent_process = 1`
     - Process single message (the task)
     - Output final JSON result to stdout
     - Exit

#### 1.3 Modify `src/utils/config.h`

Add to `ralph_config_t`:
```c
// Subagent configuration
int max_subagents;           // Default: 5
int subagent_timeout;        // Default: 300 seconds
```

#### 1.4 Modify `src/utils/config.c`

- Add parsing for `max_subagents` and `subagent_timeout` from JSON
- Add defaults in initialization

#### 1.5 Modify `src/core/ralph.h`

Add SubagentManager to RalphSession:
```c
typedef struct {
    SessionData session_data;
    TodoList todo_list;
    ToolRegistry tools;
    ProviderRegistry provider_registry;
    LLMProvider* provider;
    MCPClient mcp_client;
    SubagentManager subagent_manager;  // NEW
} RalphSession;
```

#### 1.6 Modify `src/core/ralph.c`

- Initialize SubagentManager in `ralph_init_session()`
- Cleanup SubagentManager in `ralph_cleanup_session()`

#### 1.7 Modify `src/tools/tools_system.c`

In `register_builtin_tools()`:
```c
// Only register subagent tools if NOT running as subagent (prevent nesting)
if (!session->subagent_manager.is_subagent_process) {
    if (register_subagent_tool(registry, &session->subagent_manager) != 0) return -1;
    if (register_subagent_status_tool(registry, &session->subagent_manager) != 0) return -1;
}
```

**Note**: This requires passing session context to `register_builtin_tools()` or using a different registration pattern.

#### 1.8 Modify `src/core/main.c`

Add `--subagent` flag handling:
```c
// Parse arguments
int subagent_mode = 0;
char *subagent_task = NULL;
char *subagent_context = NULL;

for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--subagent") == 0) {
        subagent_mode = 1;
    } else if (strcmp(argv[i], "--task") == 0 && i + 1 < argc) {
        subagent_task = argv[++i];
    } else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc) {
        subagent_context = argv[++i];
    }
    // ... existing flag handling
}

if (subagent_mode) {
    return ralph_run_as_subagent(subagent_task, subagent_context);
}
```

#### 1.9 Modify `Makefile`

Add to `TOOL_SOURCES`:
```makefile
TOOL_SOURCES := $(SRCDIR)/tools/tools_system.c \
                $(SRCDIR)/tools/shell_tool.c \
                $(SRCDIR)/tools/file_tools.c \
                ... \
                $(SRCDIR)/tools/subagent_tool.c  # NEW
```

Add test definitions (following existing pattern):
```makefile
TEST_SUBAGENT_TOOL_C_SOURCES = $(TESTDIR)/tools/test_subagent_tool.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_SUBAGENT_TOOL_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_SUBAGENT_TOOL_OBJECTS = $(TEST_SUBAGENT_TOOL_C_SOURCES:.c=.o) $(TEST_SUBAGENT_TOOL_CPP_SOURCES:.cpp=.o)
TEST_SUBAGENT_TOOL_TARGET = $(TESTDIR)/test_subagent_tool
```

---

### Phase 2: Core Implementation Details

#### 2.1 Unique ID Generation

Use random hex string (16 chars):
```c
static void generate_subagent_id(char *id_out) {
    static const char hex[] = "0123456789abcdef";
    unsigned char random_bytes[SUBAGENT_ID_LENGTH / 2];

    // Read from /dev/urandom or use time-based fallback
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(random_bytes, 1, sizeof(random_bytes), f);
        fclose(f);
    } else {
        // Fallback: combine time and pid
        srand(time(NULL) ^ getpid());
        for (int i = 0; i < sizeof(random_bytes); i++) {
            random_bytes[i] = rand() & 0xFF;
        }
    }

    for (int i = 0; i < sizeof(random_bytes); i++) {
        id_out[i*2] = hex[random_bytes[i] >> 4];
        id_out[i*2+1] = hex[random_bytes[i] & 0x0F];
    }
    id_out[SUBAGENT_ID_LENGTH] = '\0';
}
```

#### 2.2 Process Spawning (fork/exec pattern)

```c
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out) {
    // Check limits
    if (manager->count >= manager->max_subagents) {
        return -1;  // Too many subagents
    }

    // Generate ID
    char id[SUBAGENT_ID_LENGTH + 1];
    generate_subagent_id(id);

    // Create pipe for output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end

        // Redirect stdout to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Build exec arguments
        char *args[7];
        int arg_idx = 0;
        args[arg_idx++] = "./ralph";
        args[arg_idx++] = "--subagent";
        args[arg_idx++] = "--task";
        args[arg_idx++] = (char*)task;
        if (context) {
            args[arg_idx++] = "--context";
            args[arg_idx++] = (char*)context;
        }
        args[arg_idx] = NULL;

        execv("./ralph", args);
        // If exec fails
        _exit(127);
    }

    // Parent process
    close(pipefd[1]);  // Close write end

    // Record subagent
    int idx = manager->count++;
    manager->subagents = realloc(manager->subagents, manager->count * sizeof(Subagent));

    Subagent *sub = &manager->subagents[idx];
    strncpy(sub->id, id, SUBAGENT_ID_LENGTH + 1);
    sub->pid = pid;
    sub->status = SUBAGENT_STATUS_RUNNING;
    sub->stdout_pipe[0] = pipefd[0];
    sub->task = strdup(task);
    sub->context = context ? strdup(context) : NULL;
    sub->output = NULL;
    sub->output_len = 0;
    sub->result = NULL;
    sub->error = NULL;
    sub->start_time = time(NULL);

    strcpy(subagent_id_out, id);
    return 0;
}
```

#### 2.3 Status Checking

```c
int subagent_get_status(SubagentManager *manager, const char *subagent_id, int wait,
                        SubagentStatus *status, char **result, char **error) {
    // Find subagent
    Subagent *sub = NULL;
    for (int i = 0; i < manager->count; i++) {
        if (strcmp(manager->subagents[i].id, subagent_id) == 0) {
            sub = &manager->subagents[i];
            break;
        }
    }

    if (!sub) {
        *error = strdup("Subagent not found");
        *status = SUBAGENT_STATUS_FAILED;
        return -1;
    }

    // If already completed, return cached result
    if (sub->status == SUBAGENT_STATUS_COMPLETED ||
        sub->status == SUBAGENT_STATUS_FAILED ||
        sub->status == SUBAGENT_STATUS_TIMEOUT) {
        *status = sub->status;
        *result = sub->result ? strdup(sub->result) : NULL;
        *error = sub->error ? strdup(sub->error) : NULL;
        return 0;
    }

    // Check process status
    int wait_flags = wait ? 0 : WNOHANG;
    int proc_status;

    while (1) {
        // Check timeout
        time_t elapsed = time(NULL) - sub->start_time;
        if (elapsed > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent timed out");
            break;
        }

        pid_t result_pid = waitpid(sub->pid, &proc_status, wait_flags);

        if (result_pid == sub->pid) {
            // Process exited
            // Read remaining output
            read_subagent_output(sub);

            if (WIFEXITED(proc_status) && WEXITSTATUS(proc_status) == 0) {
                sub->status = SUBAGENT_STATUS_COMPLETED;
                sub->result = sub->output;  // Transfer ownership
                sub->output = NULL;
            } else {
                sub->status = SUBAGENT_STATUS_FAILED;
                sub->error = strdup("Subagent process failed");
            }
            break;
        } else if (result_pid == 0) {
            // Still running (WNOHANG case)
            if (!wait) {
                break;
            }
            // If waiting, sleep briefly and retry
            usleep(50000);  // 50ms
        } else {
            // Error
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("waitpid error");
            break;
        }
    }

    // Read any available output (non-blocking)
    if (sub->status == SUBAGENT_STATUS_RUNNING) {
        read_subagent_output_nonblocking(sub);
    }

    *status = sub->status;
    *result = sub->result ? strdup(sub->result) : NULL;
    *error = sub->error ? strdup(sub->error) : NULL;
    return 0;
}
```

#### 2.4 Subagent Mode Entry Point

```c
int ralph_run_as_subagent(const char *task, const char *context) {
    RalphSession session;

    // Initialize with subagent flag
    ralph_init_session(&session);
    session.subagent_manager.is_subagent_process = 1;  // Prevent tool registration

    ralph_load_config(&session);

    // Build message with optional context
    char *message;
    if (context) {
        size_t len = strlen(task) + strlen(context) + 32;
        message = malloc(len);
        snprintf(message, len, "Context: %s\n\nTask: %s", context, task);
    } else {
        message = strdup(task);
    }

    // Process single message - output goes to stdout (captured by parent)
    int result = ralph_process_message(&session, message);

    // Clean up
    free(message);
    ralph_cleanup_session(&session);

    return result;
}
```

---

### Phase 3: Testing

#### 3.1 Create `test/tools/test_subagent_tool.c`

Test cases:
1. `test_subagent_manager_init` - Manager initializes with config values
2. `test_subagent_spawn_basic` - Can spawn a subagent
3. `test_subagent_spawn_max_limit` - Respects max_subagents limit
4. `test_subagent_status_running` - Status returns running for active subagent
5. `test_subagent_status_completed` - Status returns completed with result
6. `test_subagent_status_wait` - Wait flag blocks until completion
7. `test_subagent_status_timeout` - Timeout triggers SIGKILL
8. `test_subagent_no_nesting` - Subagent mode doesn't register subagent tools
9. `test_subagent_tool_execution` - End-to-end tool call test

#### 3.2 Integration Testing

- Test spawning real subagent that executes shell commands
- Test multiple concurrent subagents
- Test timeout behavior with long-running tasks

---

## Implementation Order

1. ~~**Step 1**: Add config fields (`max_subagents`, `subagent_timeout`) to config.h/c~~ ✓ DONE
2. ~~**Step 2**: Create subagent_tool.h with all declarations~~ ✓ DONE
3. **Step 3**: Implement SubagentManager (init, cleanup)
4. **Step 4**: Implement `subagent_spawn()` with fork/exec
5. **Step 5**: Implement `subagent_get_status()` with waitpid
6. **Step 6**: Add `--subagent` flag handling in main.c
7. **Step 7**: Implement `ralph_run_as_subagent()`
8. **Step 8**: Implement tool execution functions
9. **Step 9**: Register tools in tools_system.c (conditional on !is_subagent_process)
10. **Step 10**: Update Makefile
11. **Step 11**: Write tests
12. **Step 12**: Run valgrind, fix memory issues

---

## Risk Areas & Mitigations

1. **Zombie processes**: Ensure all forked processes are properly reaped with waitpid()
2. **Pipe deadlock**: Use non-blocking reads, limit output buffer size
3. **Memory leaks**: Follow existing cleanup patterns, test with valgrind
4. **Path to ralph binary**: Use `/proc/self/exe` on Linux or argv[0] fallback
5. **Tool registration timing**: May need to refactor register_builtin_tools() to accept session context

---

## Files Summary

### New Files
- `src/tools/subagent_tool.h` - Header file ✓
- `src/tools/subagent_tool.c` - Implementation
- `test/tools/test_subagent_tool.c` - Unit tests

### Modified Files
- `src/utils/config.h` - Add config fields ✓
- `src/utils/config.c` - Parse new config fields ✓
- `src/core/ralph.h` - Add SubagentManager to RalphSession
- `src/core/ralph.c` - Initialize/cleanup SubagentManager
- `src/core/main.c` - Handle --subagent flag
- `src/tools/tools_system.c` - Register subagent tools (conditionally)
- `Makefile` - Add new source file and test target
