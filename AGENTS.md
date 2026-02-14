# ralph Project

Portable C codebase compiled with Cosmopolitan. Source in `src/`, tests in `test/` (using vendored `unity`).
See `@ARCHITECTURE.md` and `@CODE_OVERVIEW.md` for design docs. Use `ripgrep` to find existing implementations.

Always update docs (AGENTS.md, `@ARCHITECTURE.md` and `@CODE_OVERVIEW.md`) before you commit.

## Build Commands

| Command | Description |
|---------|-------------|
| `./scripts/build.sh` | Build (compact output) |
| `./scripts/build.sh -v` | Build with full output |
| `./scripts/build.sh clean` | Remove build artifacts (keeps dependencies) |
| `./scripts/build.sh distclean` | Remove everything (nuclear) |
| `make check-valgrind` | Memory safety check |
| `make update-cacert` | Refresh CA certificates (then `./scripts/build.sh clean && ./scripts/build.sh`) |

## Development Environment

- Docker devcontainer with Cosmopolitan toolchain pre-configured
- `.env` has API credentials - **never read it**, only source it
- Use `apt` for tooling only, not libraries (breaks portability)
- `--debug` flag shows HTTP traffic to LLM providers
- **Uses OpenAI, not Anthropic** - debug tool parsing in `openai_provider.c`, `parse_tool_calls` in `tools_system.c`
- Approval gates: `--yolo` disables all prompts for the session

## Code Style

**Memory safety first**: Initialize pointers, free memory, validate parameters before use.

**Functional C**: Prefer immutability, isolate mutable code, functions as first-class, operate on collections.

**Clean code**: SOLID, DRY, composition over inheritance. Small functions, testable chunks.

**No incomplete code**: No TODOs, no placeholders. If you can't implement something fully, redesign the solution.

**Delete aggressively**: Prefer editing over adding. Remove dead code during refactors. Code not written can't have bugs.

**Makefile changes**: Maintain structure and organization for AI readability. 

**Fast tests**: Use mocks for external APIs in tests where possible.

**Segfaults are critical**: Fix segfaults immediately using all debugging tools at your disposal.

## Testing

**TDD workflow**: Write a failing test first, then implement to make it pass.

**Build first**: Always build before running tests. Tests require compiled binaries.

```bash
# Build, then run full suite (preferred)
./scripts/build.sh && ./scripts/run_tests.sh

# Individual test (name from def_test in mk/tests.mk, not file path)
./scripts/build.sh test/test_<name> && ./test/test_<name>

# Test script options
./scripts/run_tests.sh        # Compact output with segfault detection
./scripts/run_tests.sh -q     # Summary only
./scripts/run_tests.sh foo    # Pattern match
```

### Valgrind Exclusions

- **Subagent tests**: fork/exec causes cascading process explosion
- **Python tests**: Platform ELF binaries lack embedded stdlib
- **SIGPIPE in timeout tests**: Expected, ignore it

## Debugging

Standard C debug tools work. Use platform-specific binaries: `.aarch64.elf` (ARM64) or `.com.dbg` (x86_64).

**Tracing**: `--ftrace` (function calls), `--strace` (syscalls). Use `-mdbg` for complete ftrace output.

**Valgrind**: Check `uname -m` first, then:
```bash
valgrind --leak-check=full ./test/test_foo.aarch64.elf  # ARM64
valgrind --leak-check=full ./test/test_foo.com.dbg      # x86_64
```

## Versioning & Releases

**Single source of truth**: Git tags (`v*`). The build generates `build/version.h` from `git describe --tags`, providing version string, git hash, and dirty flag. No version numbers in config files.

**Release**: Trigger the Release workflow from the GitHub Actions UI on `master`. Pick a bump type (major/minor/patch) and optionally provide release notes. The workflow calculates the next version from the latest tag, builds, tests, creates the tag, and publishes to GitHub Releases.

**Dev builds**: On non-tagged commits, `git describe` produces versions like `0.1.0-14-gabcdef`, giving a natural dev version with distance from the last release.

**Build-time version header** (`build/version.h`): Provides `RALPH_VERSION`, `RALPH_GIT_HASH`, `RALPH_GIT_DIRTY`, `RALPH_BUILD_VERSION`. Uses compare-and-swap to avoid unnecessary recompilation.

## Technology

C compiled with Cosmopolitan in Docker devcontainer. Libraries: mbedtls (TLS), unity (testing), SQLite (storage), HNSWLIB (vectors), PDFio (PDF), cJSON (JSON), ossp-uuid (UUIDs). Debug tools: valgrind, gdb.
