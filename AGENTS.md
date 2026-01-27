# ralph Project

ralph is a highly portable C codebase compiled using Cosmopolitan (see @COSMOPOLITAN.md for details)
Source code lives in `src/` and tests live in `test/`. Tests use `unity` which is vendored in `test/unity`
The architecture is documented as a mermaid diagram in `@ARCHITECTURE.md`, and a code overview available in `@CODE_OVERVIEW.md`

The code base is large and has most functionality implemented. Use `ripgrep` to search for code that may already be implemented.

## Build and Commands

- Build: `make`
- Remove all build artifacts, leaving dependencies: `make clean`
- Remove everything: `make distclean` (this is nuclear!)
- Run test suite: `make test`
- Run Valgrind to check memory safety: `make check-valgrind`
- Update CA certificates: `make update-cacert`

### SSL/TLS CA Certificates

Ralph embeds Mozilla's CA certificate bundle for portable SSL/TLS across all platforms. The bundle is stored in `src/network/embedded_cacert.c` and applied in `src/network/http_client.c` via `CURLOPT_CAINFO_BLOB`.

To refresh the CA bundle (recommended periodically for security updates):
```bash
make update-cacert
make clean && make
```

### Development Environment

This project is developed inside a Docker devcontainer, defined in `.devcontainer/devcontainer.json` and `.devcontainer/Dockerfile`.
The container is pre-configured with access to the entire Cosmopolitan toolchain, including PATH configuration and environment variables.
The container has a valid `.env` file that is configured to allow ralph to leverage real APIs. **DO NOT EVER READ THIS FILE**. Source it to use it.
You may use apt to install missing tooling that you need, however do not use it to install libraries as dependencies of ralph, as that will break ralph's portability. Any ralph dependencies should be built locally using Cosmopolitan, with recipes in the Makefile.
Run ralph with `--debug` to see all HTTP requests and data exchange from upstream LLM providers.

### LLM Provider

**The development environment uses OpenAI as the LLM provider, NOT Anthropic.** When debugging tool call parsing or response handling issues, investigate the OpenAI code paths (`parse_tool_calls` in `tools_system.c`, `openai_provider.c`) rather than the Anthropic paths.

### Approval Gate Flags

Ralph has an approval gate system that prompts before executing certain tool categories. For development:

- `--yolo`: Disable all approval gates for the session. Use when you trust the LLM to execute tools without confirmation.
- `--allow "tool:pattern"`: Add a specific pattern to the session allowlist
- `--allow-category=<category>`: Set a category (file_write, shell, network, etc.) to allow without prompting

Example for development workflow:
```bash
./ralph --yolo "implement the feature"
```

## Code Style

Write modern, memory safe defensive portable C code. Always initialize pointers, always free memory, always check your parameters before you use them as a few examples.
Prefer functional programming techniques as far as you can accomplish them in C. This means immutability, or isolating mutable code, functions as first-order entities, operating on collections over single entities, etc.
In general, prefer composition over inheretance when building complex ideas out of simple ones.
Always observe SOLID, DRY and other clean coding principles as you work. Aggressively refactor code that is not fit for purpose.
Try to keep functions on the shorter side, and break complex logic down into small, easily testable chunks.
Never write TODOs, placeholder code, or anything that won't satisfy the current requirement completely. If you encounter something that you either don't want to, or can't, write the complete implementation of, redesign or restructure your solution to be decomposable.
Prefer changing code to writing new code - we use source control, we can go back if we need. We don't want a lot of dead code littering this codebase.
Try to delete as much code as you can (while still being productive, not destructive). Code that isn't written doesn't cause bugs. Aggressively remove dead code and remove unneded functionality during refactors.
When changing the Makefile, ensure you maintain the organization and structure of the file to ensure it is AI optimized 

## Testing

When developing, practice test-driven-development in the following way:
- write a test that describes your desired behavior; new feature, or the **result** of a bug fix, ensuring the test **fails**
- fix the test by implementing the new feature or bug fix

Tests may write temporary conversation files during testing - clean up test artifacts before performing a full test run.

Unit tests should be written against mocks where possible, however tests that need an LLM can be run against a real, live API.
The development environment is configured so that integration tests will successfully connect to a real LLM API.
Any upstream related API errors in tests are due to **your code**, either in the test or the code itself.

As you work, segfaults are critical issues and must be fixed immediately when encountered. Start investigating using `valgrind`.
Your work is not considered complete unless it is accompanied by automated unit tests and has been checked through `valgrind`.

`valgrind` will sometimes fail with a SIGPIPE error if you're running timeout tests. This is ok.

### Integration Tests Requiring API Keys

Some tests require API credentials to pass. Before running these, source the environment file:

```bash
source .env && ./test/test_vector_db_tool
```

The `test_vector_db_tool` test specifically requires OpenAI Embeddings API access for the `test_vector_db_add_text` test case. Without sourcing `.env`, this test will fail with "Expected 1 Was 0" because the embeddings API call fails without authentication.

### Subagent Tests and Valgrind

**Subagent tests are excluded from valgrind entirely.** Do not run valgrind on subagent tests.

The subagent tests use `fork()` and `execv()` to spawn child ralph processes. Running these under valgrind causes a cascading process explosion that crashes the devcontainer:
- Each test spawns a subagent → valgrind traces it
- Each subagent initializes a full ralph session (tools, MCP, etc.)
- MCP initialization may fork additional server processes
- All processes make real API calls under valgrind's slow execution
- This exhausts memory and crashes the devcontainer

To test subagent memory safety, run the subagent code paths directly without the fork/exec layer.

### Approval Gate Tests and Valgrind

**Subagent approval tests (`test_subagent_approval`) are excluded from valgrind** for the same reasons as subagent tests.

Approval gate tests that require user prompting use mock inputs or test non-interactive code paths. The `gate_prompter` module handles TTY interaction, which cannot be easily mocked in unit tests. Integration tests for approval gates focus on:
- Non-interactive mode behavior (stdin is not a TTY)
- Allowlist bypass (patterns that skip prompting)
- Rate limiting logic
- Category configuration
- Error message formatting

## Debugging

You can use all normal C/C++ debug tooling on Cosmopolitan C projects. You must only remember that you need to check it against
platform specific binaries, such as .aarch64.elf (aarch64) and .com.dbg (amd64)

### Function and System Call Tracing

Cosmopolitan binaries support built-in tracing via CLI flags passed to the fat binary:

- `--ftrace`: Log every function call entry/exit at runtime. Useful for understanding control flow without a debugger.
- `--strace`: Log every system call made by the program. Useful for diagnosing I/O, networking, and OS-level issues.

Note that `--ftrace` output may be incomplete due to inlining under optimized builds; compile with the `-mdbg` cosmocc flag for more complete traces.

### Valgrind and Architecture

**IMPORTANT: Before running valgrind, check the host architecture and use the correct binary:**

```bash
uname -m
```

- If `aarch64` (ARM64): Use `.aarch64.elf` files with valgrind
- If `x86_64` (AMD64): Use `.com.dbg` files with valgrind

Example on aarch64:
```bash
valgrind --leak-check=full ./test/test_foo.aarch64.elf
```

Example on x86_64:
```bash
valgrind --leak-check=full ./test/test_foo.com.dbg
```

### Python Tests and Valgrind

**Python tests (`test_python_tool`, `test_python_integration`) are excluded from valgrind.**

The Python stdlib is embedded into the APE binary using `zipcopy`, but valgrind requires running the platform-specific ELF binaries (`.aarch64.elf` or `.com.dbg`). These platform-specific binaries don't have the stdlib embedded, so Python initialization fails with "No module named 'encodings'".

To test Python tool memory safety, rely on the unit tests for the C code paths (argument parsing, JSON formatting, cleanup functions) which don't require the Python interpreter to be fully initialized.

## Technology

- Source code is C
- Compiled with Cosmopolitan
- Inside a Docker devcontainer running Debian
- Using `mbedtls` for TLS/SSL support, and `unity` for unit testing
- Using `SQLite` for persistent task storage
- Using `HNSWLIB` for vector similarity search
- Using `PDFio` for PDF text extraction
- Using `cJSON` for JSON parsing
- With `valgrind`, `gdb` and all common C debugging tools preconfigured

## Architecture

Ralph's architecture has been documented in `@ARCHITECTURE.md` and `@CODE_OVERVIEW.md` extensively.
