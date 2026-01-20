# Plan: Embed Persistent Python REPL in Ralph

## Overview

Embed a Python 3.12 interpreter into ralph as a tool, allowing the LLM to execute Python code with persistent state across invocations. This enables computational tasks, data manipulation, and scripting within conversations.

## Current State

| Component | Status | Location |
|-----------|--------|----------|
| `libpython3.12.a` | Built (56.7 MB) | `build/libpython3.12.a` |
| Python headers | Available | `build/python-include/` |
| Standard library | Compiled, needs embedding | `python/build/results/py-tmp/lib/` |
| Build integration | Makefile targets exist | `make python` |
| Tool system | Ready for new tools | `src/tools/` |

## Architecture Decisions

### 1. Persistent Interpreter (Not Per-Call)

Initialize Python once at ralph startup, maintain state across tool calls:

```c
// At startup
Py_Initialize();
PyObject *main_module = PyImport_AddModule("__main__");
PyObject *globals = PyModule_GetDict(main_module);

// Each tool call runs in same namespace
PyRun_String(code, Py_file_input, globals, globals);

// At shutdown only
Py_Finalize();
```

**Rationale**: Enables variables, imports, and defined functions to persist between calls - essential for iterative development and multi-step computations.

### 2. Stdlib Embedding Strategy

Embed stdlib into ralph.com binary using Cosmopolitan's APE zip feature:

```bash
zip -qr ralph.com lib/
```

Set `PYTHONHOME=/zip` before `Py_Initialize()` so Python finds modules at `/zip/lib/python3.12/`.

**Rationale**: Single portable binary, no external dependencies, consistent with Cosmopolitan philosophy.

### 3. Output Capture

Redirect Python's stdout/stderr to StringIO before each execution:

```python
import sys, io
_stdout = io.StringIO()
_stderr = io.StringIO()
sys.stdout, sys.stderr = _stdout, _stderr
```

After execution, read captured output and restore original streams.

**Rationale**: Captures `print()` output and error messages for tool results without affecting ralph's own I/O.

### 4. Timeout Enforcement

Use `signal(SIGALRM, handler)` with `alarm(timeout_seconds)` to interrupt long-running code:

```c
static volatile sig_atomic_t python_timed_out = 0;

void python_timeout_handler(int sig) {
    python_timed_out = 1;
    PyErr_SetInterrupt();  // Raises KeyboardInterrupt in Python
}
```

**Rationale**: Prevents infinite loops from hanging ralph. KeyboardInterrupt is catchable but provides clean abort.

---

## Priority Task List

Tasks are ordered by dependency. Complete each task before starting tasks that depend on it.

### P1: Create `src/tools/python_tool.h` ✅ COMPLETE

**Depends on:** Nothing
**Blocks:** P2, P4, P5
**Status:** Complete - Header file created at `src/tools/python_tool.h`

Define the public interface for the Python tool:

```c
#ifndef PYTHON_TOOL_H
#define PYTHON_TOOL_H

#include "tools_system.h"

#define PYTHON_MAX_CODE_SIZE (1024 * 1024)  // 1MB
#define PYTHON_DEFAULT_TIMEOUT 30           // seconds
#define PYTHON_MAX_OUTPUT_SIZE (512 * 1024) // 512KB

typedef struct {
    char *code;
    int timeout_seconds;
    int capture_stderr;
} PythonExecutionParams;

typedef struct {
    char *stdout_output;
    char *stderr_output;
    char *exception;
    int success;
    double execution_time;
    int timed_out;
} PythonExecutionResult;

// Lifecycle - called from ralph main
int python_interpreter_init(void);
void python_interpreter_shutdown(void);

// Tool registration
int register_python_tool(ToolRegistry *registry);

// Execution
int execute_python_tool_call(const ToolCall *tool_call, ToolResult *result);

// Cleanup
void cleanup_python_params(PythonExecutionParams *params);
void cleanup_python_result(PythonExecutionResult *result);

#endif
```

---

### P2: Create `src/tools/python_tool.c`

**Depends on:** P1
**Blocks:** P3, P4, P5

Implement the Python tool (~800-1000 lines). Structure:

1. **Includes and static globals**
   - Python.h
   - Interpreter state pointers (main_module, globals dict)
   - Timeout flag

2. **`python_interpreter_init()`**
   - setenv("PYTHONHOME", "/zip", 1)
   - Py_Initialize()
   - Get __main__ module and globals dict
   - Set up output capture infrastructure
   - Return 0 on success

3. **`python_interpreter_shutdown()`**
   - Py_Finalize()

4. **`register_python_tool()`**
   - Define parameters:
     * "code" (string, required) - Python code to execute
     * "timeout" (number, optional) - Max seconds, default 30
   - Call register_tool()

5. **`execute_python_tool_call()`**
   - Parse JSON arguments
   - Validate code (size, basic safety)
   - Call execute_python_code_internal()
   - Format result JSON
   - Return via ToolResult

6. **`execute_python_code_internal()`**
   - Set up timeout signal handler
   - Redirect stdout/stderr to StringIO
   - Start timer
   - PyRun_String(code, Py_file_input, globals, globals)
   - Cancel alarm
   - Capture output from StringIO
   - Check for exceptions (PyErr_Occurred)
   - Restore stdout/stderr
   - Populate PythonExecutionResult

7. **`format_python_result_json()`**
   - Build JSON: {"stdout": "...", "stderr": "...", "success": true, ...}
   - Escape special characters

8. **Helper functions**
   - extract_json_string_value() - reuse pattern from shell_tool
   - escape_json_string()

9. **Cleanup functions**

---

### P3: Update Makefile for Python compilation and linking

**Depends on:** P2
**Blocks:** P4, P5, P6, P7

Add Python tool to build system:

```makefile
# Add to TOOL_SOURCES
TOOL_SOURCES := $(SRCDIR)/tools/tools_system.c \
                $(SRCDIR)/tools/shell_tool.c \
                $(SRCDIR)/tools/file_tools.c \
                $(SRCDIR)/tools/links_tool.c \
                $(SRCDIR)/tools/todo_manager.c \
                $(SRCDIR)/tools/todo_tool.c \
                $(SRCDIR)/tools/todo_display.c \
                $(SRCDIR)/tools/memory_tool.c \
                $(SRCDIR)/tools/pdf_tool.c \
                $(SRCDIR)/tools/vector_db_tool.c \
                $(SRCDIR)/tools/python_tool.c

# Add Python to includes
INCLUDES = -I$(CURL_DIR)/include \
           ... \
           -I$(BUILDDIR)/python-include

# Add Python to libraries
PYTHON_LINK_LIBS = $(PYTHON_LIB) -lm -lpthread -ldl -lutil
ALL_LIBS := $(CURL_LIB) $(MBEDTLS_LIBS) $(CJSON_LIB) $(PDFIO_LIB) $(ZLIB_LIB) $(PYTHON_LINK_LIBS)

# Add dependency
$(TARGET): $(PYTHON_LIB)
```

---

### P4: Integrate with ralph startup/shutdown

**Depends on:** P1, P2, P3
**Blocks:** P5, P7, P8

Modify `src/tools/tools_system.c`:

```c
#include "python_tool.h"

int register_builtin_tools(ToolRegistry *registry) {
    // ... existing registrations ...

    if (register_python_tool(registry) != 0) {
        fprintf(stderr, "Failed to register python tool\n");
        return -1;
    }

    return 0;
}
```

Modify `src/ralph.c` (or appropriate init location):

```c
#include "tools/python_tool.h"

// In initialization
if (python_interpreter_init() != 0) {
    fprintf(stderr, "Warning: Python interpreter failed to initialize\n");
}

// In cleanup
python_interpreter_shutdown();
```

---

### P5: Create `test/tools/test_python_tool.c`

**Depends on:** P1, P2, P3, P4
**Blocks:** P6, P7

Write unit tests:

```c
#include "unity.h"
#include "../../src/tools/python_tool.h"
#include "../../src/tools/tools_system.h"

static ToolRegistry registry;

void setUp(void) {
    init_tool_registry(&registry);
    python_interpreter_init();
}

void tearDown(void) {
    cleanup_tool_registry(&registry);
}

void test_python_tool_registration(void) {
    TEST_ASSERT_EQUAL(0, register_python_tool(&registry));
    TEST_ASSERT_EQUAL(1, registry.count);
    TEST_ASSERT_EQUAL_STRING("python", registry.functions[0].name);
}

void test_python_simple_execution(void) {
    register_python_tool(&registry);

    ToolCall call = {
        .id = "test-1",
        .name = "python",
        .arguments = "{\"code\": \"print(1 + 1)\"}"
    };
    ToolResult result = {0};

    execute_python_tool_call(&call, &result);

    TEST_ASSERT_EQUAL(1, result.success);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "\"stdout\": \"2\\n\""));

    free(result.result);
}

void test_python_persistent_state(void) {
    register_python_tool(&registry);

    ToolCall call1 = {
        .id = "test-2a",
        .name = "python",
        .arguments = "{\"code\": \"x = 42\"}"
    };
    ToolResult result1 = {0};
    execute_python_tool_call(&call1, &result1);
    TEST_ASSERT_EQUAL(1, result1.success);
    free(result1.result);

    ToolCall call2 = {
        .id = "test-2b",
        .name = "python",
        .arguments = "{\"code\": \"print(x * 2)\"}"
    };
    ToolResult result2 = {0};
    execute_python_tool_call(&call2, &result2);
    TEST_ASSERT_EQUAL(1, result2.success);
    TEST_ASSERT_NOT_NULL(strstr(result2.result, "\"stdout\": \"84\\n\""));
    free(result2.result);
}

void test_python_timeout(void) {
    register_python_tool(&registry);

    ToolCall call = {
        .id = "test-3",
        .name = "python",
        .arguments = "{\"code\": \"while True: pass\", \"timeout\": 1}"
    };
    ToolResult result = {0};

    execute_python_tool_call(&call, &result);

    TEST_ASSERT_EQUAL(0, result.success);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "timed_out"));

    free(result.result);
}

void test_python_exception_handling(void) {
    register_python_tool(&registry);

    ToolCall call = {
        .id = "test-4",
        .name = "python",
        .arguments = "{\"code\": \"raise ValueError('test error')\"}"
    };
    ToolResult result = {0};

    execute_python_tool_call(&call, &result);

    TEST_ASSERT_EQUAL(0, result.success);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "ValueError"));
    TEST_ASSERT_NOT_NULL(strstr(result.result, "test error"));

    free(result.result);
}

void test_python_import_stdlib(void) {
    register_python_tool(&registry);

    ToolCall call = {
        .id = "test-5",
        .name = "python",
        .arguments = "{\"code\": \"import json; print(json.dumps({'a': 1}))\"}"
    };
    ToolResult result = {0};

    execute_python_tool_call(&call, &result);

    TEST_ASSERT_EQUAL(1, result.success);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "{\\\"a\\\": 1}"));

    free(result.result);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_python_tool_registration);
    RUN_TEST(test_python_simple_execution);
    RUN_TEST(test_python_persistent_state);
    RUN_TEST(test_python_timeout);
    RUN_TEST(test_python_exception_handling);
    RUN_TEST(test_python_import_stdlib);

    python_interpreter_shutdown();

    return UNITY_END();
}
```

---

### P6: Add test target to Makefile

**Depends on:** P3, P5
**Blocks:** P7

```makefile
TEST_PYTHON_SOURCES = $(TESTDIR)/tools/test_python_tool.c \
                      $(SRCDIR)/tools/python_tool.c \
                      $(SRCDIR)/tools/tools_system.c \
                      $(UNITY_DIR)/unity.c

TEST_PYTHON_TARGET = $(TESTDIR)/test_python_tool

$(TEST_PYTHON_TARGET): $(TEST_PYTHON_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(PYTHON_LINK_LIBS) $(ZLIB_LIB)

test-python: $(TEST_PYTHON_TARGET)
	./$(TEST_PYTHON_TARGET)

# Add to main test target
test: test-python ...
```

---

### P7: Automate stdlib embedding

**Depends on:** P3, P4, P6
**Blocks:** P8

Add Makefile target to embed Python stdlib into ralph binary:

```makefile
embed-stdlib: $(TARGET)
	@if [ -d "python/build/results/py-tmp/lib" ]; then \
		echo "Embedding Python stdlib..."; \
		cd python/build/results/py-tmp && zip -qr ../../../../$(TARGET) lib/; \
		echo "Stdlib embedded. Binary size: $$(ls -lh ../../../../$(TARGET) | awk '{print $$5}')"; \
	else \
		echo "Warning: Python stdlib not found. Run 'make python' first."; \
	fi

# Update main build target
all: $(TARGET) embed-stdlib
```

---

### P8: Valgrind verification and end-to-end testing

**Depends on:** P4, P6, P7
**Blocks:** Nothing (final task)

1. Run `make check-valgrind` - verify no memory leaks in Python tool
2. Run ralph with `--debug` and test Python tool interactively
3. Verify state persistence across calls
4. Verify timeout kills runaway code without crashing
5. Verify exceptions are caught and reported cleanly

---

## Task Dependency Graph

```
P1 ─────────┬───────────────────────────────┐
            │                               │
            ▼                               │
P2 ─────────┬───────────────────────────────┤
            │                               │
            ▼                               │
P3 ─────────┬───────────┬───────────────────┤
            │           │                   │
            ▼           │                   │
P4 ─────────┬───────────┤                   │
            │           │                   │
            │           ▼                   ▼
            │       P5 ─┬───────────────────┘
            │           │
            │           ▼
            │       P6 ─┐
            │           │
            ▼           ▼
P7 ─────────┬───────────┘
            │
            ▼
P8 (done)
```

---

## Tool Definition (for LLM)

The registered tool will appear to the model as:

```json
{
  "name": "python",
  "description": "Execute Python code in a persistent interpreter. Variables, imports, and function definitions persist across calls. Use for calculations, data processing, and scripting tasks.",
  "parameters": {
    "type": "object",
    "properties": {
      "code": {
        "type": "string",
        "description": "Python code to execute. Can be multiple lines. Variables persist between calls."
      },
      "timeout": {
        "type": "number",
        "description": "Maximum execution time in seconds. Default: 30"
      }
    },
    "required": ["code"]
  }
}
```

## Tool Result Format

```json
{
  "stdout": "output from print() statements",
  "stderr": "error output if any",
  "exception": "exception message if code raised an error, null otherwise",
  "success": true,
  "execution_time": 0.023,
  "timed_out": false
}
```

---

## Risk Mitigations

### Memory Leaks
- Run valgrind on test suite
- Track all Python object references with Py_INCREF/DECREF
- Use Py_XDECREF for potentially-NULL pointers

### Interpreter Corruption
- Catch exceptions properly with PyErr_Fetch/PyErr_Clear
- Don't leave interpreter in error state between calls
- Consider interpreter restart if state becomes corrupted

### Timeout Race Conditions
- Use sig_atomic_t for timeout flag
- Clear alarm before any early return
- Handle EINTR in any blocking calls

### Code Injection
- The tool is designed to run arbitrary code (that's its purpose)
- Security comes from ralph's overall sandboxing, not this tool
- Consider adding optional restrictions (no file I/O, no network) as future enhancement

### Binary Size
- Python lib adds ~18MB compressed to binary
- Acceptable for portable distribution
- Can strip unused stdlib modules if size becomes issue

---

## Success Criteria

1. `make` compiles ralph with Python support without warnings
2. `make test` passes all Python tool tests
3. `make check-valgrind` shows no memory leaks in Python tool
4. ralph binary includes embedded stdlib (~38MB total with Python)
5. LLM can execute Python code and receive results
6. State persists: `x = 1` in one call, `print(x)` works in next call
7. Timeout kills runaway code without crashing ralph
8. Exceptions are caught and reported cleanly
