# AGENT.md

Universal configuration file for AI coding assistants working with the Ralph HTTP client project.

## Project Overview

Ralph is a modular, memory-safe HTTP client for AI language models built with Cosmopolitan C. The project emphasizes zero external dependencies, comprehensive testing, and cross-platform portability.

### Architecture
- **Modular C codebase**: 15 core components with dedicated headers
- **Test-driven development**: Unity framework with comprehensive coverage
- **Memory safety**: Valgrind verification required for all code
- **Build system**: Self-contained Makefile with dependency management
- **Portability**: Cosmopolitan Libc for cross-platform compatibility

## Commands

### Build Commands
```bash
make              # Build main executable
make clean        # Remove build artifacts
make distclean    # Remove artifacts and dependencies
make realclean    # Complete clean slate
```

### Test Commands
```bash
make test                    # Build and run all tests
make check                   # Alias for test
make check-valgrind          # Run tests with memory analysis
make check-valgrind-all      # Include HTTP tests (may have library noise)
```

### Development Commands
```bash
gdb ./ralph.aarch64.elf      # Debug with GDB (use .aarch64.elf format)
./ralph --help               # Show usage information
./ralph --strace             # Debug with system call tracing
./ralph --ftrace             # Debug with function tracing
```

## Code Style and Conventions

### C Programming Standards
- **Compiler**: Cosmopolitan C (`cosmocc`) with `-Wall -Wextra -Werror -O2 -std=c11`
- **Error handling**: Always check return values from system calls and library functions
- **Memory management**: Strict malloc/free patterns with proper initialization
- **Buffer safety**: Initialize all buffers with `= {0}`, use `memcpy()` for known-length copies
- **Composition over inheritance**: Favor modular design patterns

### Code Organization
- **Source**: `src/*.c` and `src/*.h` files
- **Tests**: `test/test_*.c` files using Unity framework
- **Dependencies**: Static linking, built from source in `deps/`
- **No stubs**: Complete implementation required, break down complex tasks

## Build and Dependencies

### Dependency Management
- **Static linking**: All dependencies compiled and linked statically
- **Automatic building**: Makefile handles dependency fetching and compilation  
- **Core dependencies**: libcurl 8.4.0, mbedTLS 3.5.1, Unity test framework
- **Zero external runtime dependencies**: Self-contained executables

### Cosmopolitan Integration
- **Toolchain**: Pre-configured in `/opt/cosmos` with appropriate environment variables
- **Documentation**: See `COSMOPOLITAN.md` for detailed information about Cosmopolitan C
- **Multi-format output**: 
  - `ralph` - Portable APE executable
  - `ralph.aarch64.elf` - Native ELF for debugging
  - `ralph.com.dbg` - Debug symbols

## Testing Guidelines

### Test Requirements
- **Complete coverage**: Every component must have comprehensive unit tests
- **Memory safety**: All tests must pass Valgrind analysis with zero errors/leaks
- **Error conditions**: Test null parameters, edge cases, and failure paths
- **Integration**: Cross-component functionality verification

### Expected Test Behavior
- **Shell timeout test**: May fail under Valgrind due to execution overhead (acceptable)
- **HTTP tests**: Excluded from default Valgrind due to library noise
- **Test execution**: Individual test binaries in `test/` directory

## Security Considerations

### Memory Safety Protocol
- **Valgrind mandatory**: Zero tolerance for memory errors, leaks, or buffer overruns
- **Buffer initialization**: Always initialize with `= {0}` pattern
- **Bounds checking**: Use static analysis-friendly patterns
- **Shell command validation**: Security validation for shell tool execution

### Git and Version Control Security
- **Selective commits**: Never use `git add -A` or `git add .`
- **Artifact exclusion**: Never commit build artifacts (*.o, *.elf, *.dbg)
- **Infrastructure protection**: Absolutely forbidden to delete `.devcontainer/`, `.github/`, `Makefile`
- **Pre-commit verification**: Always verify no infrastructure files marked for deletion

## Development Workflow

### Standard Development Cycle
1. **Search existing code**: Verify functionality hasn't been implemented
2. **Write defensive code**: Proper error checking and memory management
3. **Create comprehensive tests**: Unit tests in `test/test_*.c`
4. **Memory verification**: Must pass `make check-valgrind`
5. **Integration testing**: Verify with main application
6. **Selective commit**: Source files only, verify with `git status`

### Memory Safety Workflow
1. **Feature development**: Use defensive C patterns
2. **Unit testing**: Comprehensive test coverage
3. **Valgrind verification**: Zero errors/leaks required
4. **Build verification**: `make clean && make test` must pass
5. **Commit verification**: No build artifacts or infrastructure deletions

### Component Completion Criteria
- All composite components compilable
- Comprehensive unit tests passing
- Valgrind analysis clean
- Integration with main application verified
- No segmentation faults or broken states

## Tool Integration Notes

### Debugging
- **GDB requirement**: Use `ralph.aarch64.elf` format (native ELF)
- **APE format limitation**: Main `ralph` executable not GDB-compatible
- **Debug symbols**: Available in `ralph.com.dbg` (may have compatibility issues)

### File Organization
- **Source control**: Only commit .c/.h files, Makefiles, tests, documentation
- **Build artifacts**: Automatically generated, excluded from version control
- **Dependencies**: Built locally, not committed to repository

This configuration ensures robust, portable, and memory-safe C development suitable for production use across multiple platforms.