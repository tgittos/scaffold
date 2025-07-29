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

## Project Overview

This is a simple C "Hello World" program built using GNU Autotools (autoconf/automake) and Cosmopolitan Libc. The project generates a single executable called `hello-world` that prints "Hello, World!" to stdout.

## Build System

### Cosmopolitan Libc Build (Recommended)
```bash
cosmocc src/main.c -o hello-world
```

### Autotools Build
1. **Initial setup** (if configure script doesn't exist):
   ```bash
   autoreconf -i
   ```

2. **Configure the build**:
   ```bash
   ./configure
   ```

3. **Build the project** (may fail due to cosmocc space-in-arguments limitation):
   ```bash
   make
   ```

4. **Clean build artifacts** (removes executables, object files, cosmopolitan variants):
   ```bash
   make clean
   ```

5. **Full clean** (removes ALL generated files including configure, Makefiles, autotools cache):
   ```bash
   make distclean
   ```

**Important**: `make clean` removes build artifacts but leaves autotools-generated files. `make distclean` removes everything generated, returning to source-only state.

**Note**: When asked to "clean" the project, always use `make distclean` to fully clean everything back to source-only state.

## Architecture

- **configure.ac**: Autoconf configuration defining the project metadata and build requirements
- **Makefile.am**: Top-level automake configuration with proper clean targets for cosmopolitan artifacts
- **src/Makefile.am**: Defines the hello-world binary target from main.c
- **src/main.c**: Simple C program that prints "Hello, World!"

## Code Quality Standards

When modifying C code in this project:
- All functions must check return values and handle errors appropriately
- Use `const` qualifiers where possible
- Initialize all variables at declaration
- Prefer stack allocation over dynamic allocation for small, fixed-size data
- Use safe string functions (`strncpy`, `snprintf`) over unsafe variants
- Add bounds checking for array access
- Use meaningful variable names and avoid magic numbers
- Comment complex logic and document function contracts

## Test-Driven Development

You practice strict test-driven development (TDD):
- Write tests BEFORE implementing functionality
- All code must have corresponding automated tests
- Tests should cover normal cases, edge cases, and error conditions
- Use a lightweight C testing framework or simple assertion-based tests
- Run tests after every code change to ensure no regressions
- Maintain high test coverage for all non-trivial functions
- Test memory management (no leaks, proper cleanup)
- Test error handling paths and boundary conditions

## Testing Framework

This project uses Unity for unit testing and valgrind for memory analysis:

### Running Tests
```bash
# Build and run tests
make check

# Run tests with valgrind (memory leak detection)
make check-valgrind
```

### Test Structure
- `test/` - Contains all test files
- `test/unity/` - Unity testing framework (vendored)
- `test/test_main.c` - Main test runner
- Tests follow the pattern: `void test_function_name(void)`

### Writing Tests
```c
#include "unity/unity.h"

void test_my_function(void) {
    TEST_ASSERT_EQUAL_INT(expected, actual);
    TEST_ASSERT_NULL(pointer);
    TEST_ASSERT_TRUE(condition);
}
```

### Memory Testing
- All tests should pass under valgrind with zero leaks
- Use `make check-valgrind` to verify memory safety
- Tests run with `--leak-check=full --error-exitcode=1`

## Development Environment

- Uses Cosmopolitan Libc toolchain (cosmocc) for cross-platform compilation
- Produces multiple executable formats (.com, .aarch64.elf, etc.)
- Development container includes autotools, cosmopolitan toolchain, and valgrind
- Unity testing framework vendored in `test/unity/`