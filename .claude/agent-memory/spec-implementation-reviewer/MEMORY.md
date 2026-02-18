# Spec-Implementation Reviewer Memory

## Project Structure
- `lib/` = shared library (libagent.a), `src/` = binary entry points
- `lib/db/` = SQLite-backed stores (goal_store, action_store, task_store, etc.)
- `lib/tools/` = tool implementations (goap_tools, orchestrator_tool, etc.)
- `lib/orchestrator/` = supervisor, orchestrator, role_prompts, goap_state
- `mk/lib.mk` = library module sources, `mk/tests.mk` = test definitions
- `ORCHESTRATOR.md` = specification for the scaffold orchestrator feature

## Key Patterns
- Services DI container: `services.h` holds all store pointers, wired via `*_set_services()`
- Tools use global `Services*` pointers set during agent_init
- Tool registration conditional on `app_home_get_app_name()` for scaffold-only tools
- `def_test` for standalone tests, `def_test_lib` for tests linking libagent.a
- SQLite DAL sharing: goal_store and action_store share a DAL via `*_create_with_dal()`

## Review Checklist for This Codebase
- Check memory safety: NULL checks, proper free paths, no leaks in error paths
- Verify strdup/malloc failure handling in mapper functions
- Check that global static pointers (g_services) are properly wired before use
- Verify all new stores added to services_create_default AND services_destroy
- Verify test targets added to mk/tests.mk with correct link dependencies
- Check that spec struct fields match implementation struct fields
