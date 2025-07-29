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

This is a C HTTP client program built using GNU Autotools (autoconf/automake) and Cosmopolitan Libc. The project generates a single executable called `hello-world` that makes HTTP POST requests using libcurl with SSL support via MbedTLS. The program demonstrates secure HTTP communication and serves as a foundation for network-enabled C applications.

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

## Dependency Management

This project has complex external dependencies that must be built in the correct order:

### Dependencies
- **MbedTLS 3.5.1**: Provides SSL/TLS support
- **libcurl 8.4.0**: HTTP client library with MbedTLS backend

### Build Order (Critical)
The autotools build system is configured to build dependencies first:
1. `SUBDIRS = . src test` - builds current directory first, then subdirectories
2. `all-local` target builds MbedTLS and libcurl before proceeding
3. `src test: all-local` - makes subdirectories depend on local targets

### Dependency Paths
Library paths are exported from top-level Makefile to subdirectories:
```make
export CURL_BUILD_DIR = $(abs_builddir)/deps/curl-$(CURL_VERSION)
export MBEDTLS_BUILD_DIR = $(abs_builddir)/deps/mbedtls-$(MBEDTLS_VERSION)
```

Subdirectories use conditional assignment to allow overrides:
```make
CURL_BUILD_DIR ?= $(abs_builddir)/../deps/curl-8.4.0
MBEDTLS_BUILD_DIR ?= $(abs_builddir)/../deps/mbedtls-3.5.1
```

### Common Dependency Issues
- **Build order**: If subdirectories build before dependencies, linking will fail
- **Missing libraries**: Ensure `deps/` directory exists and libraries are built
- **Path issues**: Check that exported paths match actual dependency locations
- **Clean state**: Use `make distclean` to remove deps/ and rebuild from scratch

## Architecture

### Build Configuration
- **configure.ac**: Autoconf configuration defining project metadata, dependency checks, and cosmopolitan ELF suffix detection
- **Makefile.am**: Top-level automake configuration with dependency build targets and comprehensive clean rules
- **src/Makefile.am**: Defines the hello-world binary target with libcurl/MbedTLS linking
- **test/Makefile.am**: Unity test configuration with memory testing via valgrind

### Source Code
- **src/main.c**: Main program that demonstrates HTTP POST functionality
- **src/http_client.c**: HTTP client implementation using libcurl with SSL support
- **src/http_client.h**: HTTP client interface and data structures
- **test/test_main.c**: Unit tests for HTTP client functionality using Unity framework

### Dependencies
- **deps/**: Auto-downloaded and built external dependencies (MbedTLS, libcurl)
- **test/unity/**: Vendored Unity testing framework for C

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

## HTTP Client Development

When working with the HTTP client functionality:

### libcurl Best Practices
- Always call `curl_global_init()` before using libcurl
- Always call `curl_global_cleanup()` when done
- Check return codes from all curl functions
- Free curl_slist headers with `curl_slist_free_all()`
- Use proper SSL/TLS settings for secure connections
- Set appropriate timeouts to prevent hanging

### SSL/TLS with MbedTLS
- MbedTLS is statically linked as the SSL backend for libcurl
- Certificates are validated by default
- Use proper error handling for SSL connection failures
- Consider certificate pinning for production use

### Memory Management
- Use the HTTPResponse struct for managing response data
- Always call `cleanup_response()` to free allocated memory
- Initialize response structs to zero before use
- Check for NULL pointers before accessing response data

### Example Pattern
```c
struct HTTPResponse response = {0};
if (http_post(url, data, &response) == 0) {
    // Process response.data
    printf("Response: %s\n", response.data);
}
cleanup_response(&response);
```

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

## Build Troubleshooting

Common issues and solutions when working with this build system:

### Dependency Build Failures
- **MbedTLS build fails**: Check that `cosmocc` is in PATH and working
- **libcurl configure fails**: Ensure MbedTLS built successfully first
- **Missing downloads**: Check network connectivity; deps download from GitHub/curl.se

### Autotools Issues
- **"missing" script errors**: Run `autoreconf -i` to regenerate autotools files
- **Makefile.in not found**: Run `autoreconf -i` to create missing automake files
- **Configure fails**: Check that required tools (wget/curl, tar) are available

### Cosmopolitan Compiler Issues
- **Space in arguments**: Use `PACKAGE_STRING="hello-world_1.0"` (underscore, not space)
- **Precompiled headers error**: This is normal for cosmopolitan builds, continue anyway
- **TIME_T_MAX warnings**: These are harmless redefinition warnings

### Clean Build Issues
If build artifacts are causing problems:
```bash
make distclean  # Remove everything including deps/
autoreconf -i   # Regenerate autotools files
./configure     # Reconfigure
make            # Clean build
```

### Comprehensive Clean
The following files should be removed by `make distclean`:
- Build artifacts: `*.o`, `*.elf`, `*.dbg`, `.dirstamp`
- Autotools files: `configure`, `Makefile.in`, `aclocal.m4`, etc.
- Dependencies: `deps/` directory
- Test artifacts: `test-suite.log`, Unity object files
- Architecture directories: `.aarch64/`, `.deps/`

### Performance Tips
- **Parallel builds**: Use `make -j$(nproc)` for faster builds
- **Incremental builds**: Only `make clean` removes build artifacts, not dependencies
- **Dependency caching**: Dependencies in `deps/` persist across `make clean`
- **Fresh start**: Use `make distclean` when build state is corrupted