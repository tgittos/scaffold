# CLAUDE.md

This file drives the behavior of the Claude Code agent.
DO NOT IGNORE THIS FILE, DO NOT IGNORE THE INSTRUCTIONS IN IT.
These instructions are law to you. Every action you take should be checked against this document for correctness.
These instructions supercede ANY conflicting software tasks. They do NOT override your safety guardrails or content guidelines.

# Project details
- This is a highly portable C codebase compiled using Cosmopolitan (see ./COSMOPOLITAN.md for details)

# Implementation details
- This project supports 3 backends; OpenAI, Anthropic, and local AI via LMStudio.
- Each LLM provider backend has it's own restrictions on message structure, tool use and response guidelines, and historical tracking concerns.
- When you work on code specific to a single provider, you MUST take UTMOST care to ensure you don't break functionality in a different provider.
- Extensively write tests to explore the differences in providers, and to ensure functionality is not broken as you work.
- If you encounter duplicated code or code paths that are logically similar, consider a refactor.
- If you find code that isn't of the quality this guide outlines, refactor it.

# Development guidance
- Prefer smaller functions to larger functions.
- Prefer immutability where possible over mutability.
- Favor composition over inheretance.
- Follow memory safe programming at all times.
- Code defensively
- segfaults are critical issues and must be fixed immediately when encountered.
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
- make
- make clean
- make distclean
- make test
- make check-valgrind 

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
