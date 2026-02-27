# ralph Project

Portable C codebase compiled with Cosmopolitan. Source in `src/`, tests in `test/` (using vendored `unity`).
See `@COSMOPOLITAN.md`, `@ARCHITECTURE.md`, `@CODE_OVERVIEW.md` for design docs.

Always update docs (AGENTS.md, `@ARCHITECTURE.md` and `@CODE_OVERVIEW.md`) before you commit.

## Build & Run

| Command | Description |
|---------|-------------|
| `./scripts/build.sh` | Build (compact output) |
| `./scripts/build.sh -v` | Build with full output |
| `./scripts/build.sh clean` | Remove build artifacts (keeps dependencies) |
| `./scripts/build.sh distclean` | Remove everything (nuclear) |
| `make check-valgrind` | Memory safety check |
| `make update-cacert` | Refresh CA certificates (then `./scripts/build.sh clean && ./scripts/build.sh`) |

### Output binaries

Build produces binaries in `out/`:
- **`out/scaffold`** - Main cross-platform fat binary. Use this to run the app.
- `out/scaffold.aarch64.elf` - ARM64 debug binary (for valgrind/gdb on ARM)
- `out/scaffold.com.dbg` - x86_64 debug binary (for valgrind/gdb on x86)

`build/` contains intermediate artifacts (object files, `scaffold.base` pre-embedding binary). Do not run from `build/`.

### Running

```bash
# Interactive mode
source .env && out/scaffold

# One-shot mode (process single message and exit)
source .env && out/scaffold "your message here"

# Pipe input
source .env && echo "your message" | out/scaffold

# With specific model
source .env && out/scaffold --model gpt-4o "your message"

# With debug HTTP traffic
source .env && out/scaffold --debug "your message"

# Disable approval gates
source .env && out/scaffold --yolo "your message"
```

### CLI flags

| Flag | Description |
|------|-------------|
| `--debug` | Show HTTP traffic to LLM providers |
| `--no-stream` | Disable response streaming |
| `--json` | Enable JSON output mode |
| `--yolo` | Disable all approval gates for this session |
| `--model <name>` | Select model (e.g. `gpt-4o`) |
| `--home <path>` | Override home directory (default: `~/.local/scaffold`) |
| `--login` | Log in to OpenAI via OAuth (ChatGPT subscription) |
| `--logout` | Log out of OpenAI OAuth session |

## Development Environment

- Docker devcontainer with Cosmopolitan toolchain pre-configured
- `.env` has API credentials - **never read it**, only source it
- Use `apt` to install tooling only, not libraries (breaks portability)
- **Python**: Use `uv` for Python package/project management. Install it if not already present (`curl -LsSf https://astral.sh/uv/install.sh | sh`)

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

**Fix flaky tests**: Flaky tests are as bad as segfaults; they completely break the CI/CD system and must be fixed immediately.

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

## Debugging

Standard C debug tools work. Use platform-specific binaries: `.aarch64.elf` (ARM64) or `.com.dbg` (x86_64).
Use these binaries **only** for debugging. Use the cross platform fat binary at all other times.

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