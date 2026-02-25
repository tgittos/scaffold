# Spec-Implementation Reviewer Memory

## Project Structure
- `lib/` = shared library (libagent.a), `src/` = binary entry points
- `lib/db/` = SQLite-backed stores (goal_store, action_store, task_store, etc.)
- `lib/tools/` = tool implementations (goap_tools, orchestrator_tool, etc.)
- `lib/orchestrator/` = supervisor, orchestrator, role_prompts, goap_state
- `lib/auth/` = OAuth2 authentication (openai_login, oauth_callback_server, jwt_decode, openai_oauth_provider)
- `lib/llm/providers/` = LLM provider implementations (codex_provider, openai_provider, anthropic_provider, etc.)
- `mk/lib.mk` = library module sources, `mk/tests.mk` = test definitions
- `ORCHESTRATOR.md` = specification for the scaffold orchestrator feature

## Key Patterns
- Services DI container: `services.h` holds all store pointers, wired via `*_set_services()`
- Tools use global `Services*` pointers set during agent_init
- Tool registration conditional on `app_home_get_app_name()` for scaffold-only tools
- `def_test` for standalone tests, `def_test_lib` for tests linking libagent.a
- SQLite DAL sharing: goal_store and action_store share a DAL via `*_create_with_dal()`
- Provider vtable pattern: `OAuth2ProviderOps` struct with function pointers for auth URL, exchange, refresh
- LLM provider registry: URL-based detection, Codex registered first (priority over OpenAI)

## Review Checklist for This Codebase
- Check memory safety: NULL checks, proper free paths, no leaks in error paths
- Verify strdup/malloc failure handling in mapper functions
- Check that global static pointers (g_services) are properly wired before use
- Verify all new stores added to services_create_default AND services_destroy
- Verify test targets added to mk/tests.mk with correct link dependencies
- Check that spec struct fields match implementation struct fields
- Verify `mbedtls_platform_zeroize` is called on sensitive buffers (tokens, keys)
- Check OAuth state round-trip validation (callback state vs. auth state)
- Verify HTTP status codes are checked, not just curl return codes
- Check URL parameter encoding in auth URL construction

## Known Patterns in OAuth Implementation
- `oauth2_store.c` uses `BindText2`/`bind_text2` from `sqlite_dal.h` for parameterized queries
- Token encryption uses HKDF for key derivation + AES-256-GCM
- Refresh token rotation: try `refresh_token_rotate` first, fall back to `refresh_token`
- Codex provider: uses Responses API format (not Chat Completions), requires `chatgpt-account-id` header
- `session_configurator.c` forces streaming=true for Codex (non-streaming not supported)
- Thread-local account ID in codex_provider.c via `_Thread_local`
