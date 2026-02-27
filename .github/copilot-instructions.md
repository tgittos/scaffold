# Scaffold - Copilot Coding Agent Instructions

## Project Summary

Scaffold is a portable AI-powered development companion built in C using Cosmopolitan Libc. It produces a single binary that runs natively on Linux, macOS, Windows, and BSD systems (x86_64 and ARM64). Scaffold provides an interactive CLI for code assistance with tools for file operations, shell execution, vector database, PDF processing, Python execution, MCP protocol integration, and plugin support.

## Technology Stack

- **Language**: C (C11) with C++ (C++14) for HNSWLIB wrapper
- **Compiler**: `cosmocc` / `cosmoc++` (Cosmopolitan toolchain)
- **Build System**: GNU Make with `./scripts/build.sh` wrapper
- **Testing**: Unity framework (vendored in `test/unity/`)
- **Dependencies**: All vendored in `deps/` and built with Cosmopolitan
  - curl 8.4.0, mbedtls 3.5.1, hnswlib 0.8.0, pdfio 1.3.1, zlib 1.3.1, cJSON 1.7.18, readline 8.2, ncurses 6.4, Python 3.12

## Build Commands

Always run these commands from the repository root.

| Command | Purpose |
|---------|---------|
| `./scripts/build.sh` | Build scaffold binary (compact output) |
| `./scripts/build.sh -v` | Build with full output |
| `./scripts/build.sh clean` | Remove build artifacts, keep dependencies |
| `./scripts/build.sh distclean` | Remove everything including deps |
| `./scripts/build.sh && ./scripts/run_tests.sh` | Build and run all tests |
| `make check-valgrind` | Run memory safety checks |

### Output Binaries

Build produces binaries in `out/`:
- **`out/scaffold`** - Main cross-platform fat binary (use this to run)
- `out/scaffold.aarch64.elf` - ARM64 debug binary (for valgrind/gdb on ARM)
- `out/scaffold.com.dbg` - x86_64 debug binary (for valgrind/gdb on x86)

`build/` contains intermediate artifacts. Do not run from `build/`.

## Project Layout

```
scaffold/
├── src/                    # Application layer (thin wrapper around lib/)
│   ├── scaffold/           # Scaffold CLI
│   │   └── main.c          # Entry point, CLI parsing, orchestration modes
│   └── tools/              # Python tool integration
│       ├── python_tool.c/h         # Embedded Python interpreter
│       ├── python_extension.c/h    # Tool extension interface
│       ├── http_python.c/h         # Python HTTP extension
│       ├── verified_file_python.c/h # Python verified I/O extension
│       ├── sys_python.c/h          # Python system info extension
│       └── python_defaults/        # Default Python tool scripts
├── lib/                    # Reusable library layer
│   ├── agent/              # Agent lifecycle and session management
│   ├── session/            # Session data, conversation tracking, tokens
│   ├── db/                 # Vector DB, document store, SQLite DAL, OAuth2 store
│   ├── auth/               # OAuth2 authentication (OpenAI login, PKCE, JWT)
│   ├── llm/                # LLM provider abstraction
│   │   ├── models/         # Model capability implementations
│   │   └── providers/      # Codex, OpenAI, Anthropic, local AI providers
│   ├── network/            # HTTP client, streaming, API error handling
│   ├── policy/             # Approval gates, allowlists, protected files
│   ├── plugin/             # Plugin system (discovery, JSON-RPC, hooks)
│   ├── tools/              # Tool registry, built-in tools (memory, PDF, etc.)
│   ├── mcp/                # Model Context Protocol client
│   ├── ui/                 # Terminal UI, slash commands, status line
│   ├── ipc/                # Inter-process messaging
│   ├── orchestrator/       # GOAP supervisor, orchestrator, role prompts
│   ├── workflow/           # SQLite-backed task queue
│   ├── services/           # Dependency injection container
│   ├── updater/            # Self-update via GitHub releases
│   ├── util/               # Config, UUID, chunking, debug utilities
│   └── pdf/                # PDF text extraction
├── test/                   # Test files (mirrors src/ and lib/ structure)
│   ├── unity/              # Unity testing framework (vendored)
│   └── ...                 # Tests organized by module
├── deps/                   # Vendored dependencies
├── docs/                   # User documentation
└── mk/                     # Makefile includes
```

## Key Source Files

| File | Purpose |
|------|---------|
| `src/scaffold/main.c` | Application entry point, CLI argument parsing |
| `lib/tools/tools_system.c/h` | Tool registry and execution framework |
| `lib/llm/llm_provider.c/h` | Provider abstraction with URL-based detection |
| `lib/agent/agent.c/h` | Agent lifecycle management |
| `lib/agent/session.c/h` | Session coordinator |
| `lib/db/vector_db.c/h` | Vector database operations |
| `lib/network/http_client.c/h` | HTTP client using libcurl |
| `lib/policy/approval_gate.c/h` | Approval gate system |

## Testing

```bash
# Build and run all tests
./scripts/build.sh && ./scripts/run_tests.sh

# Run individual test
./scripts/build.sh test/test_<name> && ./test/test_<name>

# Test script options
./scripts/run_tests.sh -q     # Summary only
./scripts/run_tests.sh foo    # Pattern match
```

### Memory Safety (Valgrind)

```bash
uname -m  # Check architecture first
valgrind --leak-check=full ./test/test_foo.aarch64.elf  # ARM64
valgrind --leak-check=full ./test/test_foo.com.dbg      # x86_64
```

## Code Style Requirements

- Initialize all pointers
- Always free allocated memory
- Check parameters before use
- Keep functions short and testable
- No TODOs or placeholder code
- Prefer editing existing code over creating new files
- Remove dead code aggressively
- Do not install system libraries via apt - all deps must be built with Cosmopolitan

## Adding New Code

### New Tool
1. Create `lib/tools/mytool.c` and `lib/tools/mytool.h`
2. Register in `lib/tools/builtin_tools.c`
3. Add to Makefile sources
4. Create `test/tools/test_mytool.c`
5. Add test target to Makefile

### New Source File
1. Create file in appropriate `lib/` subdirectory
2. Add to corresponding sources variable in Makefile
3. Create matching test file

## Configuration

Scaffold uses `scaffold.config.json` for configuration:

```json
{
  "api_url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o",
  "openai_api_key": "sk-...",
  "context_window": 128000
}
```

Environment variables `OPENAI_API_KEY` and `ANTHROPIC_API_KEY` are auto-detected. OAuth login (`--login`) enables ChatGPT subscription access via the Codex provider without an API key.
