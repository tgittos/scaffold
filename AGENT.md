# CLAUDE.md

This file drives the behavior of the Claude Code agent.

# ralph Project

ralph is a highly portable C codebase compiled using Cosmopolitan (see ./COSMOPOLITAN.md for details)
Source code lives in `src/` and tests live in `/test`. Tests use `unity` which is vendored in `test/unity`

Major modules:
- Core logic is in `src/core`
- network code in `src/network`
- user-session related code is in `src/session`
- LLM specific systems in `src/llm`
- LLM tools are in `src/tools`
- `src/utils` contains generic utility code

The code base is large and has most basic functionality implemented. Use `ripgrep` to search for code that may already be implemented.

## Build and Commands

- Build: `make`
- Remove all build artifacts, leaving dependencies: `make clean`
- Remove everything: `make distclean`
- Run test suite: `make test`
- Run Valgrind to check memory safety: `make check-valgrind`

### Development Environment

This project is developed inside a Docker devcontainer, defined in `.devcontainer/devcontainer.json` and `.devcontainer/Dockerfile`.
The container is pre-configured with access to the entire Cosmopolitan toolchain, including PATH configuration and environment variables.
The container has a valid `.env` file that is configured to allow ralph to leverage real APIs

## Code Style

Write modern, memory safe defensive portable C code. Always initialize pointers, always free memory, always check your parameters before you use them as a few examples.
Prefer functional programming techniques as far as you can accomplish them in C. This means immutability, or isolating mutable code, functions as
first-order entities, operating on collections over single entities, etc.
In general, prefer composition over inheretance when building complex ideas out of simple ones.
Always observe SOLID, DRY and other clean coding principles as you work. Aggressively refactor code that is not fit for purpose.
Try to keep functions on the shorter side, and break complex logic down into small, easily testable chunks.
Never write TODOs, placeholder code, or anything that won't satisfy the current requirement completely. If you encounter something that you either don't want to, or can't,
write the complete implementation of, redesign or restructure your solution to be decomposable.

## Testing

When developing, practice test-driven-development in the following way:
- write a test that describes your desired behavior; new feature, or the **result** of a bug fix, ensuring the test **fails**
- fix the test by implementing the new feature or bug fix

Tests write to `CONVERSATION.md`, so it should be deleted before peforming a full test run.

Unit tests should be written against mocks where possible, however tests that need an LLM can be run against a real, live API.
The development environment is configured so that integration tests will successfully connect to a real LLM API.
Any upstream related API errors in tests are due to **your code**, either in the test or the code itself.

As you work, segfaults are critical issues and must be fixed immediately when encountered. Start investigating using `valgrind`.
Your work is not considered complete unless it is accompanied by automated unit tests and has been checked through `valgrind`.

## Technology

- Source code is C
- Compiled with Cosmopolitan
- Inside a Docker devcontainer running Debian
- Using `mbedtls` for TLS/SSL support, `readline` for CLI functionality, and `unity` for unit testing
- With `valgrind`, `gdb` and all common C debugging tools preconfigured

## Architecture

Ralph's architecture has been documented in `ARCHITECTURE.md` extensively.