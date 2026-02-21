# Ralph - Copilot Coding Agent Instructions

## Project Summary

Ralph is a portable AI-powered development companion built in C using Cosmopolitan Libc. It produces a single binary (~65MB) that runs natively on Linux, macOS, Windows, and BSD systems (x86_64 and ARM64). Ralph provides an interactive CLI for code assistance with tools for file operations, shell execution, vector database, PDF processing, Python execution, and MCP protocol integration.

## Technology Stack

- **Language**: C (C11) with C++ (C++14) for HNSWLIB wrapper
- **Compiler**: `cosmocc` / `cosmoc++` (Cosmopolitan toolchain)
- **Build System**: GNU Make
- **Testing**: Unity framework (vendored in `test/unity/`)
- **Dependencies**: All vendored in `deps/` and built with Cosmopolitan
  - curl 8.4.0, mbedtls 3.5.1, hnswlib 0.8.0, pdfio 1.3.1, zlib 1.3.1, cJSON 1.7.18, readline 8.2, ncurses 6.4, Python 3.12

## Build Commands

Always run these commands from the repository root.

| Command | Purpose |
|---------|---------|
| `make` | Build ralph binary with embedded Python stdlib |
| `make clean` | Remove build artifacts, keep dependencies |
| `make distclean` | Remove everything including deps (nuclear option) |
| `make test` | Build and run all 35+ test executables |
| `make check-valgrind` | Run memory safety checks (excludes Python/subagent tests) |

### Build Sequence

1. Dependencies are built automatically on first `make` if missing
2. `make` compiles all sources, links ralph, then embeds Python stdlib
3. Tests must be built before running: `make test` handles this

### Build Notes

- First build downloads and compiles all dependencies (may take several minutes)
- Python stdlib embedding uses `zipcopy` tool from Cosmopolitan
- Do not install system libraries via apt - all dependencies must be built with Cosmopolitan for portability

## Project Layout

```
ralph-python/
├── src/                    # Main source code
│   ├── core/               # Entry point (main.c) and orchestration (ralph.c)
│   ├── cli/                # Slash commands (/memory)
│   ├── llm/                # LLM provider abstraction
│   │   ├── providers/      # OpenAI, Anthropic, local AI implementations
│   │   └── models/         # Model-specific implementations (GPT, Claude, etc.)
│   ├── tools/              # Tool system (25+ tools)
│   ├── db/                 # Vector database (HNSWLIB backend)
│   ├── session/            # (migrated to lib/session/)
│   ├── network/            # HTTP client (libcurl wrapper)
│   ├── mcp/                # Model Context Protocol client
│   ├── pdf/                # PDF extraction (pdfio)
│   └── utils/              # Config, chunking, formatting utilities
├── test/                   # Test files (mirror src/ structure)
│   ├── unity/              # Unity testing framework
│   ├── core/, tools/, etc. # Test files by category
│   └── mock_api_server.c   # Mock server for testing
├── deps/                   # Vendored dependencies
├── build/                  # Build artifacts and tools
├── python/                 # Cosmopolitan Python 3.12 build
└── Makefile                # Build orchestration (~800 lines)
```

## Key Source Files

| File | Purpose |
|------|---------|
| `src/scaffold/main.c` | Application entry point, CLI argument parsing |
| `src/tools/tools_system.c` | Tool registry and execution framework |
| `src/llm/llm_provider.c` | Provider abstraction with URL-based detection |
| `lib/session/conversation_tracker.c` | Conversation history with vector DB persistence |
| `src/db/vector_db.c` | Vector database operations |
| `lib/network/http_client.c` | HTTP client using libcurl |

## Testing

### Running Tests

```bash
# Run all tests
make test

# Run specific test
./test/test_main
./test/test_file_tools
./test/test_vector_db

# Tests requiring API keys (source .env first)
source .env && ./test/test_vector_db_tool
```

### Test Categories

- **Core tests**: `test_main`, `test_ralph`, `test_incomplete_task_bug`
- **Tool tests**: `test_tools_system`, `test_file_tools`, `test_shell_tool`, `test_memory_tool`, `test_python_tool`
- **Session tests**: `test_conversation_tracker`, `test_token_manager`, `test_conversation_compactor`
- **Database tests**: `test_vector_db`, `test_vector_db_tool`, `test_document_store`
- **Network tests**: `test_http_client`, `test_http_retry`, `test_messages_array_bug`

### Memory Safety (Valgrind)

```bash
# Check host architecture first
uname -m

# ARM64 (aarch64): use .aarch64.elf binaries
valgrind --leak-check=full ./test/test_foo.aarch64.elf

# x86_64: use .com.dbg binaries
valgrind --leak-check=full ./test/test_foo.com.dbg
```

**Valgrind Exclusions**:
- Python tests (`test_python_tool`, `test_python_integration`) - stdlib only in APE binary
- Subagent tests (`test_subagent_tool`) - fork/exec causes cascading process explosion

## Compiler Flags

```makefile
CFLAGS := -Wall -Wextra -Werror -O2 -std=c11 -DHAVE_PDFIO
CXXFLAGS := -Wall -Wextra -Werror -O2 -std=c++14 -Wno-unused-parameter -Wno-unused-function
```

The `-Werror` flag means all warnings are errors - code must compile cleanly.

## Development Workflow

1. **Read existing code first** - Use ripgrep (`rg`) to search for similar implementations
2. **Write failing test** - Practice TDD: test first, then implement
3. **Implement feature/fix** - Follow code style guidelines
4. **Run tests**: `make test`
5. **Check memory safety**: `make check-valgrind` (if applicable)
6. **Verify clean build**: `make clean && make`

## Code Style Requirements

- Initialize all pointers
- Always free allocated memory
- Check parameters before use
- Keep functions short and testable
- No TODOs or placeholder code
- Prefer editing existing code over creating new files
- Remove dead code aggressively

## Configuration

Scaffold uses `scaffold.config.json` for configuration:

```json
{
  "api_url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o",
  "openai_api_key": "sk-...",
  "context_window": 128000,
  "max_tokens": -1
}
```

Environment variables `OPENAI_API_KEY` and `ANTHROPIC_API_KEY` are auto-detected.

## Output Binaries

After `make`:
- `scaffold` - Main APE fat binary (portable)
- `scaffold.com.dbg` - x86_64 debug binary (for GDB/Valgrind)
- `scaffold.aarch64.elf` - ARM64 debug binary (for GDB/Valgrind)

## Common Issues

| Issue | Solution |
|-------|----------|
| Missing dependencies | Run `make` - dependencies build automatically |
| Test fails with "Expected 1 Was 0" | Source `.env` for API-dependent tests |
| Valgrind SIGPIPE error on timeout tests | Expected behavior, ignore |
| Python test fails in valgrind | Use APE binary, not platform-specific ELF |
| Build fails after clean | Ensure deps/ exists, run `make` to rebuild |

## Adding New Code

### New Tool
1. Create `src/tools/mytool.c` and `src/tools/mytool.h`
2. Register in `src/tools/tools_system.c`
3. Add to `TOOL_SOURCES` in Makefile
4. Create `test/tools/test_mytool.c`
5. Add test target to Makefile

### New Source File
1. Create file in appropriate `src/` subdirectory
2. Add to corresponding `*_SOURCES` variable in Makefile
3. Create matching test file

## Trust These Instructions

These instructions have been validated against the actual codebase. Trust them for:
- Build commands and sequences
- Test execution patterns
- File locations and structure
- Valgrind usage patterns

Only search for additional information if these instructions are incomplete or produce errors.
