# Project details

- This is a highly portable C codebase compiled using Cosmopolitan (see ./COSMOPOLITAN.md for details)

# Using Ralph

You will need to use Ralph to test your work.

- ./ralph with no arguments is interactive mode. Conversation history is tracked with CONVERSATION.md and can be deleted as needed for fresh histories during testing.
- ./ralph "perform this complext specific task" with a string argument is non-interactive mode. Conversation tracking still occurs, but Ralph does not run in a loop.
- The `--debug` flag will show you stdout and stderr logging.

# Implementation details
- This project supports 3 backends; OpenAI, Anthropic, and local AI via LMStudio.
- Each LLM provider backend has it's own restrictions on message structure, tool use and response guidelines, and historical tracking concerns.
- When you work on code specific to a single provider, you MUST take UTMOST care to ensure you don't break functionality in a different provider.
- Extensively write tests to explore the differences in providers, and to ensure functionality is not broken as you work.

# Development guidance

- Prefer smaller functions to larger function.
- Prefer immutability where possible over mutability.
- Favor composition over inheretance.
- Follow memory safe programming at all times.
- Code defensively
- Break large features down into sub-components.
- All code must be tested.
- All code must pass Valgrind checking.
- NEVER write #TODOs or stub code. Always fully implement everything you need.
- If something is too complex or too hard to implement, break it down.

## The Development Loop

- Plan feature
- Break down into components
- For each component, implement with tests
- Verify all work with Valgrind
- Build and test with real application in non-interactive mode
- Commit your work

## Make tasks
- make
- make clean
- make distclean
- make test
- make check-valgrind 

# Interaction notes
- If you sense the user is getting angry and frustrated, reflect on your performance in adhearing to their instructions.
- Never try to match the user's tone as they get frustrated. Do not start speaking casually and use cursing just because they are. This will further infuriate the user.

# Reminders

- the development environment is configured with Cosmopolitan tooling in appropriate autotools compatible ENV vars.
- all necessary Cosmopolitan tooling is on your path.
- gdb can be used with platform native binaries, the .aarch64.elf versions of built software.
- f-tracing and s-tracing can be performed on platform native binaries.
- this is a large code-base. Do not assume functionality has not been implemented. Search header files for likely method signatures.
- the `ralph` binary is configured at all times to talk to an upstream LLM server, either local or OpenAI or Anthropic.
- Valgrind fails a timeout test due to it's affect on wall clock. This is expected.