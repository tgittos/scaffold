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
Try to keep functions on the shorter side, and break complex logic down into manageable chunks.

## Testing

When developing, practice test-driven-development in the following way:
- write a test that describes your desired behavior; new feature, or the **result** of a bug fix, ensuring the test **fails**
- fix the test by implementing the new feature or bug fix

Unit tests should be written against mocks where possible, however tests that need an LLM can be run against a real, live API.
The development environment is configured so that integration tests will successfully connect to a real LLM API.
Any upstream related API errors in tests are due to **your code**, either in the test or the code itself.

As you work, segfaults are critical issues and must be fixed immediately when encountered. Start investigating using `valgrind`.

## Technology

- Source code is C
- Compiled with Cosmopolitan
- Inside a Docker devcontainer running Debian
- Using `mbedtls` for TLS/SSL support, `readline` for CLI functionality, and `unity` for unit testing
- With `valgrind`, `gdb` and all common C debugging tools preconfigured

## Architecture

Ralph's architecture has been documented in `ARCHITECTURE.md` extensively.

# Development guidance
- Start with Valgrind when investigating segfaults.
- Break large features down into sub-components.
- All code must be tested.
- All code must pass Valgrind checking.
- NEVER write #TODOs or stub code. Always fully implement everything you need.
- If something is too complex or too hard to implement, break it down.
- Leverage design patterns
- Follow DRY, SOLID, and other good development practices
- This is a large, well tested codebase. Use `ripgrep` to search for things
- This code has not been released - you do not need to version source code or ensure backward compatability

## Test Driven Development
- Use test-driven-development as you work
- Write tests to intended implementations or refactor targets - do not write tests for current functionality that you already know you will change
- Use a red/green loop - write a failing test and make the test pass by implementing the functionality
- Practice test-driven bug fixing - write a failing test that reproduces a bug, then fix the test by fixing the bug

## The Development Loop
- Plan feature
- Break down into components
- For each component, implement in with test-driven-development
- Verify all work with Valgrind
- Build and test with real application in non-interactive mode
- Commit your work

## Fixing bugs
- When fixing bugs, either at the user request, or bugs you have found yourself, write a test first to confirm the bug.
- The test you write MUST reproduce the bug. The test MUST fail if you reproduce the bug.
- You cannot fix a bug that you haven't confirmed in a test.
- Ensure your reproduction test passes when you fix the issue.

## Make tasks

The tests take some time to run the entire suite. Prefer to run individual tests.
Always delete the CONVERSATION.md file before running the test suite or evaluating the performace of the final ralph tool.

# Interaction notes
- If you sense the user is getting angry and frustrated, reflect on your performance in adhearing to their instructions.
- Never try to match the user's tone as they get frustrated. Do not start speaking casually and use cursing just because they are. This will further infuriate the user.
- Do not be overly complimentary or obseqious. Treat the user as an equal.

# Using Ralph
You will need to use Ralph to test your work.

- ./ralph with no arguments is interactive mode. Conversation history is tracked with CONVERSATION.md and can be deleted as needed for fresh histories during testing.
- ./ralph "perform this complext specific task" with a string argument is non-interactive mode. Conversation tracking still occurs, but Ralph does not run in a loop.
- The `--debug` flag will show you stdout and stderr logging.

Ralph is configured by it's `.env` file to be live and talking to a real AI backend. The real AI API backend is running. It is always running.
Do not assume API failures are OK because "you're tseting against a real API server". These API failures indicate problems with your code.
Do not ask `ralph` to perform complex tasks that would change this codebase. If you need to test how ralph breaks down a complex request, make it a read only requeset.

# Reminders
- the development environment is configured with Cosmopolitan tooling in appropriate autotools compatible ENV vars.
- all necessary Cosmopolitan tooling is on your path.
- gdb can be used with platform native binaries, the .aarch64.elf versions of built software.
- f-tracing and s-tracing can be performed on platform native binaries.
- this is a large code-base. Do not assume functionality has not been implemented. Search header files for likely method signatures.
- the `ralph` binary is configured at all times to talk to an upstream LLM server, either local or OpenAI or Anthropic.
- Valgrind fails a timeout test due to it's affect on wall clock. This is expected.
- Unless you are actively working on something, at all times, the project must build and the test suite must pass
