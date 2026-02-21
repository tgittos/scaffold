# Building from Source

Scaffold is written in C and compiled with [Cosmopolitan](https://github.com/jart/cosmopolitan), producing Actually Portable Executables that run on Linux, macOS, Windows, FreeBSD, and OpenBSD across x86_64 and ARM64.

## Using the devcontainer

The easiest way to build is with the included devcontainer, which has the Cosmopolitan toolchain pre-configured:

```bash
# Build the devcontainer image
docker build -t scaffold-dev .devcontainer/

# Build scaffold
docker run --rm --privileged \
    -v /your/checkout/path:/workspaces/ralph \
    -w /workspaces/ralph/scaffold \
    scaffold-dev \
    ./scripts/build.sh
```

The `--privileged` flag registers the APE binary loader for Cosmopolitan executables.

If you're using VS Code or a compatible editor, open the project in the devcontainer directly for an integrated development experience.

## Build commands

| Command | Description |
|---------|-------------|
| `./scripts/build.sh` | Build with compact output |
| `./scripts/build.sh -v` | Build with full output |
| `./scripts/build.sh clean` | Remove build artifacts (keeps dependencies) |
| `./scripts/build.sh distclean` | Remove everything including dependencies |

## Running tests

Always build before running tests:

```bash
# Build and run the full test suite
./scripts/build.sh && ./scripts/run_tests.sh

# Run tests with compact output
./scripts/run_tests.sh

# Summary only
./scripts/run_tests.sh -q

# Run tests matching a pattern
./scripts/run_tests.sh auth

# Build and run a single test
./scripts/build.sh test/test_config && ./test/test_config
```

Test names come from definitions in `mk/tests.mk`, not file paths.

## Memory checking

```bash
make check-valgrind
```

Check your architecture first with `uname -m`, then run valgrind against the appropriate binary:

```bash
# ARM64
valgrind --leak-check=full ./test/test_foo.aarch64.elf

# x86_64
valgrind --leak-check=full ./test/test_foo.com.dbg
```

## Debugging

Standard C debug tools work with platform-specific binaries (`.aarch64.elf` for ARM64, `.com.dbg` for x86_64).

### Tracing

```bash
# Function call tracing
./scaffold --ftrace "your message"

# System call tracing
./scaffold --strace "your message"

# Complete ftrace output (use -mdbg build)
```

### GDB

```bash
gdb ./scaffold.aarch64.elf   # ARM64
gdb ./scaffold.com.dbg       # x86_64
```

## Dependencies

All dependencies are vendored or linked at build time:

| Library | Purpose |
|---------|---------|
| libcurl | HTTP client |
| mbedtls | TLS/SSL |
| readline | Interactive line editing |
| ncurses | Terminal control |
| zlib | Compression |
| cJSON | JSON parsing |
| SQLite | Persistent storage |
| HNSWLIB | Vector similarity search |
| PDFio | PDF text extraction |
| ossp-uuid | UUID generation |
| Python 3.12 | Embedded Python interpreter |
| Unity | Testing framework |

No external package managers or system libraries are needed at runtime.

## Versioning

Version numbers come from git tags. The build generates `build/version.h` from `git describe --tags`:

- Tagged commits: `0.2.0`
- Dev builds: `0.2.0-14-gabcdef` (14 commits after v0.2.0)

### Releasing

Releases are triggered from the GitHub Actions UI on the `master` branch. Pick a bump type (major/minor/patch) and optionally provide release notes. The workflow handles version calculation, building, testing, tagging, and publishing.
