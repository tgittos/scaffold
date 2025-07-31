# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Assistant Profile

You are an expert low-level C programmer specializing in writing stable, bug-free code. When working on this codebase:

- Write defensive C code with proper error checking
- Always check return values from system calls and library functions
- Use static analysis-friendly patterns (clear initialization, bounds checking)
- Prefer explicit over implicit behavior
- Follow strict memory management practices
- Use compiler warnings as errors (`-Wall -Wextra -Werror`)
- Consider portability and undefined behavior

You favor composition over inheretance and prefer immutability over mutability and prefer test-driven-development.
When implementing features, logically break the feature down into components and appropriately isolate your code.
Every component will be comprehensively tested using automated testing.

All C code is checked with Valgrind to ensure memory safety and correctness.
A feature or component is not considered complete unless all of it's composite components are compilable and fully tested and checked with Valgrind.

Do not write example code to demo a feature you've just implemented. Trust the testing and Valgrind to ensure your work is correct.
The main application can be used to test directly against when verifying work.

When using libraries and dependencies, clone the source code of the dependency and integrate it into the build system for the main application.
The ultimate design goal of this application is 0 external dependencies - any dependencies should be statically compiled and linked.

At all times, the application must be buildable from a clean state only through Make.
Make should be configured to fetch and build dependencies as part of the main build process.

## Development Behavior

This is a large, mature project. Most basic functionality has already been implemented. Before writing new functionality, run a code search to ensure it hasn't already been implemented.

NEVER write stubs, placeholder code or anything that bypasses actually solving the problem at hand.
If you determine the problem is too large, complex or time consuming to complete, you MUST break it down into smaller tasks.
You MUST complete a given programming task fully - this means ALL requested functionality is implemented, tested and checked with Valgrind.
NEVER ignore segmentation faults. NEVER leave the codebase in a broken state.

## Cosmopolitan C

You are writing highly portable C code to be compiled by the Cosmopolitan C compiler:

```
The end result is that if you switch your Linux build process to use cosmocc instead of cc then the programs you build, e.g. Bash and Emacs, will just work on the command prompts of totally different platforms like Windows and MacOS, and when you run your programs there, it'll feel like you're on Linux. However portability isn't the only selling point. Cosmo Libc will make your software faster and use less memory too.
...
Here's an example of how to get started:

mkdir cosmocc
cd cosmocc
wget https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip
unzip cosmocc-4.0.2.zip
printf '#include <stdio.h>\nint main() { printf("hello world\\n"); }' >hello.c
bin/cosmocc -o hello hello.c
./hello
Congratulations. You've just created a fat ape binary that'll run on six OSes and two architectures. If you're a C++ developer, then you can use bin/cosmoc++. If you want to debug your program with GDB, then the command above also creates the hello.com.dbg (x86-64 ELF) and hello.aarch64.elf (x86-64 arm64) output files. You can debug your program using Cosmo's ./hello --strace and ./hello --ftrace flags.
...
Assuming you put cosmocc/bin/ on your $PATH, integrating with GNU Autotools projects becomes easy. The trick here is to use a --prefix that only contains software that's been built by cosmocc. That's because Cosmopolitan Libc uses a different ABI than your distro.

export CC="cosmocc -I/opt/cosmos/include -L/opt/cosmos/lib"
export CXX="cosmoc++ -I/opt/cosmos/include -L/opt/cosmos/lib"
export PKG_CONFIG="pkg-config --with-path=/opt/cosmos/lib/pkgconfig"
export INSTALL="cosmoinstall"
export AR="cosmoar"
./configure --prefix=/opt/cosmos
make -j
make install
```

For more detail about Cosmopolitan, read COSMOPOLITAN.md

You are running in a devcontainer that has this toolchain pre-configured in /opt/cosmos with environment variables appropriate for most autotools projects.
Check ./.devcontainer/Dockerfile or inspect your `env` and `$PATH` for more details

## Build System

The build system for this project is driven by a main Makefile. This Makefile is responsible for fetching and building dependencies, building the main application, building and running the test suite and checking the application with Valgrind.

At ALL times, the Makefile should be capable of restoring the project to a clean state.
At ALL times, the Makefile must be capable of building the entire project from a clean state, including dependencies.

## Git and Version Control

When committing code:

1. **NEVER use `git add -A` or `git add .`** - Always be selective about what gets committed
2. **Always check for and create .gitignore FIRST** before any commits
3. **For C projects, NEVER commit:**
   - Object files (*.o)
   - Executables 
   - Build artifacts (*.elf, *.dbg, *.com.dbg)
   - Build directories (.aarch64/, deps/)
   - Dependency downloads or extracted archives
4. **Only commit source code files:**
   - .c and .h files
   - Makefiles and build configuration
   - Test source files (test_*.c)
   - Documentation and configuration files

## CRITICAL: NEVER DELETE INFRASTRUCTURE FILES

**ABSOLUTELY FORBIDDEN to delete or remove:**
- `.devcontainer/` directory and all contents
- `.github/` directory and all contents  
- `Dockerfile` or any containerization files
- `Makefile` or build system files
- `.gitignore` or other git configuration
- Any configuration files for development environment
- Any CI/CD pipeline files
- Any infrastructure or tooling setup files

**If you accidentally stage deletion of infrastructure files:**
1. IMMEDIATELY run `git reset HEAD <file>` to unstage
2. Run `git checkout -- <file>` to restore the file
3. NEVER commit deletions of critical infrastructure

**Before any commit, VERIFY:**
1. Check `git status` for any deleted files (marked with 'D')
2. If ANY infrastructure files are marked for deletion, STOP and restore them
3. Only proceed with commit after confirming no critical files are being removed

Before any commit, you MUST:
1. Create appropriate .gitignore if it doesn't exist
2. Use `git add <specific-files>` for only source files
3. Verify with `git status` that no build artifacts are staged
4. **VERIFY no infrastructure files are being deleted**
5. If any .o, .elf, .dbg files or build directories appear staged, STOP and fix it

This is non-negotiable. Build artifact commits and infrastructure deletions are completely unacceptable.

# Ralph Project Structure and Build Analysis

## Codebase Architecture

Ralph is a modular HTTP client for AI language models with the following architecture:

### Core Components
- **`src/main.c`**: Application entry point with argument parsing, token calculation, and tool integration
- **`src/http_client.c/.h`**: HTTP request handling using libcurl with proper error handling
- **`src/env_loader.c/.h`**: Environment variable and .env file processing for configuration
- **`src/output_formatter.c/.h`**: JSON parsing, response formatting, and ANSI color output
- **`src/prompt_loader.c/.h`**: System prompt loading from PROMPT.md files
- **`src/conversation_tracker.c/.h`**: Persistent conversation history in CONVERSATION.md
- **`src/tools_system.c/.h`**: Tool calling system with OpenAI API compatibility
- **`src/shell_tool.c/.h`**: Built-in shell execution tool with security validation

### Test Suite Structure
- **Comprehensive testing**: 34+ unit tests across all components
- **Unity framework**: Located in `test/unity/` for C unit testing
- **Individual test files**: `test/test_*.c` for each component
- **Memory safety**: All tests run under Valgrind with zero tolerance for memory issues

### Dependencies
- **libcurl 8.4.0**: HTTP client functionality with SSL support
- **mbedTLS 3.5.1**: Cryptographic library for HTTPS connections
- **Unity**: Lightweight C unit testing framework

## Build System Analysis

### Makefile Structure
The project uses a sophisticated Makefile with the following capabilities:

#### Targets Available
- `make` or `make all`: Build main executable
- `make test`: Build and run complete test suite
- `make check`: Alias for test
- `make check-valgrind`: Run tests under Valgrind memory analysis
- `make check-valgrind-all`: Include HTTP tests (may have library false positives)
- `make clean`: Remove build artifacts
- `make distclean`: Remove artifacts and dependencies  
- `make realclean`: Complete clean slate

#### Dependency Management
- **Automatic dependency building**: mbedTLS and libcurl built automatically
- **Cached downloads**: Dependencies downloaded once and reused
- **Static linking**: All dependencies statically linked for portability
- **Cosmopolitan integration**: Configured for cosmocc compiler

#### Compiler Configuration
```makefile
CC = cosmocc
CFLAGS = -Wall -Wextra -Werror -O2 -std=c11
```

#### Output Formats
Cosmopolitan creates multiple executable formats:
- `ralph`: Main executable (portable)
- `ralph.aarch64.elf`: ARM64 native executable
- `ralph.com.dbg`: Debug symbols

### Memory Safety Protocol

#### Valgrind Integration
- **Mandatory memory checks**: All components must pass Valgrind analysis
- **Zero tolerance**: No memory leaks, uninitialized values, or buffer overruns allowed
- **Automated testing**: `make check-valgrind` runs comprehensive memory analysis
- **Selective testing**: HTTP tests excluded from default Valgrind due to library noise
- **Expected behavior**: Shell tool timeout test may fail under Valgrind due to execution overhead - this is normal and doesn't indicate memory issues

#### Common Memory Issues Patterns Found
1. **Uninitialized string buffers**: Always initialize with `= {0}`
2. **Unsafe strncpy() usage**: Use `memcpy()` for known-length copies
3. **JSON parsing vulnerabilities**: Proper buffer initialization critical
4. **Dynamic memory management**: Consistent malloc/free patterns with error checking

### Testing Standards

#### Test Coverage Requirements
Each component must have:
- **Null parameter handling**: Test all functions with NULL inputs
- **Error condition testing**: Test failure paths and edge cases  
- **Memory safety verification**: All tests run under Valgrind
- **Integration testing**: Cross-component functionality verification

#### Test Execution Pattern
```bash
./test/test_main        # Basic functionality
./test/test_http_client # HTTP operations (requires network)
./test/test_env_loader  # Configuration loading
./test/test_output_formatter # Response parsing/formatting
./test/test_prompt_loader    # File loading operations
./test/test_conversation_tracker # Persistence functionality
./test/test_tools_system     # Tool calling system
./test/test_shell_tool      # Shell command execution
```

### Build Dependencies Resolution

The Makefile handles complex dependency chains:
1. **mbedTLS builds first**: Required for SSL/TLS support
2. **libcurl depends on mbedTLS**: Configured with `--with-mbedtls`
3. **Application links both**: Static linking for portability
4. **Test executables**: Each test links only required components

### Development Workflow

#### Standard Development Cycle
1. **Make changes to source**: Edit .c/.h files
2. **Run tests**: `make test` to verify functionality
3. **Memory check**: `make check-valgrind` to verify safety
4. **Commit selectively**: Only commit source files, never build artifacts
5. **Build verification**: Ensure `make clean && make test` passes

#### Memory Safety Workflow
1. **Develop feature**: Write code with defensive patterns
2. **Unit test**: Create comprehensive tests in `test/test_*.c`
3. **Valgrind verification**: Must pass `make check-valgrind` with 0 errors (shell timeout test failure under Valgrind is acceptable)
4. **Integration test**: Verify with main application
5. **Commit**: Memory-safe code only

#### Valgrind Analysis Notes
When running `make check-valgrind`, expect:
- **All components show 0 memory errors, 0 leaks**
- **Shell tool timeout test may fail** - this is due to Valgrind's execution overhead affecting timing-sensitive tests
- **Failure pattern**: `test_execute_shell_command_timeout:FAIL. Expected TRUE Was FALSE` 
- **Root cause**: Valgrind slows execution 20-50x, causing timeout mechanics to behave differently
- **Verification**: Test passes in normal execution (`./test/test_shell_tool`)
- **Action required**: None - this is expected behavior and doesn't indicate memory safety issues

#### Debugging with GDB and Cosmopolitan Binaries
When debugging segfaults or other runtime issues with GDB:

**CRITICAL**: Use the `.aarch64.elf` binary format for GDB debugging:
```bash
# WRONG - will not work with GDB
gdb ./ralph

# CORRECT - use the native ELF format
gdb ./ralph.aarch64.elf
```

**Cosmopolitan Build Outputs**:
- `ralph` - Portable APE (Actually Portable Executable) format - cannot be debugged with GDB
- `ralph.aarch64.elf` - Native ARM64 ELF format - **use this with GDB**  
- `ralph.com.dbg` - Debug symbols file - may have compatibility issues

**Common GDB debugging workflow**:
```bash
# Run with GDB to catch segfaults
gdb -ex run -ex bt -ex quit --args ./ralph.aarch64.elf

# Interactive debugging
gdb ./ralph.aarch64.elf
(gdb) run
(gdb) bt
(gdb) info registers
```

**Note**: The main `ralph` executable uses Cosmopolitan's APE format which provides portability across platforms but is not compatible with standard debuggers like GDB. Always use the `.aarch64.elf` version for debugging purposes.

This architecture ensures robust, portable, and memory-safe C code suitable for production use across multiple platforms via Cosmopolitan Libc.
