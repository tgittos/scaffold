# Codebase Hygiene Memory

Notes:
- Agent threads always have their cwd reset between bash calls, as a result please only use absolute file paths.
- In your final response always share relevant file names and code snippets. Any file paths you return in your response MUST be absolute. Do NOT use relative paths.
- For clear communication with the user the assistant MUST avoid using emojis.
- Do not use a colon before tool calls. Text like "Let me read the file:" followed by a read tool call should just be "Let me read the file." with a period.

## Key Patterns

- Build system: Makefile includes mk/config.mk, mk/lib.mk, mk/sources.mk, mk/deps.mk, mk/tests.mk
- src/ has only src/scaffold/main.c and src/tools/ (Python tool bridge)
- lib/ is the shared library (libagent.a) with all core functionality
- Version constants use RALPH_ prefix (RALPH_VERSION, RALPH_GIT_HASH) -- this is intentional project naming
- _ralph_ prefix in Python module names (e.g., _ralph_sys, _ralph_http) is intentional internal naming
- test/stubs/ contains link-time mock substitutes for unit testing

## Known Issues (as of 2026-02-27)

- FIXED: session_configurator_cleanup() is now called in session.c:202 (develop branch)
- FIXED: codex_provider.c "future refactor" comment was cleaned up in comment audit commit
- test/stubs/subagent_stub.c exists but is not referenced in mk/tests.mk (orphaned)
- src/ralph/tools/.aarch64/ contains stale build artifacts from pre-refactor era (gitignored, local only)
- mk/config.mk INCLUDES has ghost paths: src/llm, src/session, src/utils, src/db, src/cli (directories don't exist)
- mk/tests.mk line 330 says "LIBRALPH-LINKED TESTS" but lib is now "libagent"
- test/ralph/test_main.c tests hand-constructed JSON via snprintf, no longer reflects how the codebase works
- goal_store_get_dal() and action_store_get_dal() are declared+defined but never called
- goal_store_has_active_goals(), action_store_list_ready(), action_store_skip_pending() only used in tests
- session_register_cleanup_hook() and session_unregister_all_hooks() are dead code (entire hook mechanism unused)
- supervisor.c includes <cJSON.h> but never uses it
- orchestrator.h comment for orchestrator_check_stale claims "1 hour stale guard" but code does not check time
- agent_get_session(), agent_get_session_id(), agent_get_services() static inlines never called (public API surface)
