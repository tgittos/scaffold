# Comment Auditor Memory

## Project Patterns Observed
- Security-sensitive code (lib/auth/, lib/db/oauth2_store.c) generally has good "why" comments explaining threat models, TOCTOU mitigations, and crypto choices.
- Common anti-pattern: `/* Parse JSON */`, `/* Decode base64 */` style narration comments before well-named function calls. Particularly prevalent in jwt_decode.c and codex_provider.c.
- Decoration separators (`/* ===...=== */` and `/* ---...--- */`) used heavily in oauth2_store.c and plugin_protocol.c. These add no information.
- The codebase uses a vtable pattern for providers (OAuth2ProviderOps, LLMProvider) -- comments explaining vtable contract and "why not called" for stub implementations are valuable.
- Header files (.h) have generally good API documentation using @param/@return style, but some newer headers (codex_provider.h, http_form_post.h) lack any documentation.
