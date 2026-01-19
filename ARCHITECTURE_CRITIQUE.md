# Architecture Critique: Ralph

A module-level analysis focusing on logical organization and separation of concerns.

---

## Strengths

### 1. Clear Layered Architecture

The codebase follows a reasonable layered structure:

```
Foundation → Network → LLM → Session → Database → Tools → Application
```

Dependencies generally flow downward, which is correct architectural practice.

### 2. Provider Abstraction

The `LLMProvider` interface (`src/llm/llm_provider.h:24-57`) is well-designed with proper function pointers for detection, request building, header generation, and response parsing. This enables adding new providers without modifying core code.

```c
typedef struct LLMProvider {
    ProviderCapabilities capabilities;
    int (*detect_provider)(const char* api_url);
    char* (*build_request_json)(...);
    int (*build_headers)(...);
    int (*parse_response)(...);
    int (*validate_conversation)(...);
} LLMProvider;
```

### 3. Tool System Extensibility

The `ToolRegistry` pattern with `tool_execute_func_t` callbacks (`src/tools/tools_system.h:37`) provides clean extensibility for adding new tools without modifying existing code.

### 4. Database Module Encapsulation

The `src/db/` module has excellent internal layering:

```
HNSWLIB wrapper → vector_db abstraction → thread-safe service layer → document store
```

This is textbook good design with clear separation between low-level storage and high-level document operations.

### 5. MCP Isolation

The Model Context Protocol implementation is well-contained in its own module (`src/mcp/`) with minimal coupling to the rest of the system. Tools discovered via MCP integrate cleanly through the existing ToolRegistry.

---

## Concerns

### 1. Duplicated Configuration Structures

There are two competing configuration types:

| Type | Location | Purpose |
|------|----------|---------|
| `RalphConfig` | `src/core/ralph.h:21-31` | Session-level configuration |
| `ralph_config_t` | `src/utils/config.h:6-22` | Global configuration singleton |

Both contain overlapping fields (`api_url`, `model`, `api_key`, `system_prompt`, `context_window`, `max_tokens`) but are used in different contexts. This violates DRY and creates confusion about which is the source of truth.

**Impact:** Potential for configuration drift, maintenance burden, and bugs where one is updated but not the other.

### 2. Misplaced Domain Logic in Utils

The `src/utils/` directory contains modules that aren't utilities at all:

| File | Actual Purpose |
|------|----------------|
| `pdf_processor.h/c` | Full PDF ingestion pipeline (download, extract, chunk, index) |
| `context_retriever.h/c` | Vector DB query orchestration for prompt enrichment |
| `prompt_loader.h/c` | System prompt assembly with context injection |
| `document_chunker.h/c` | Text chunking with semantic awareness |

These implement core domain logic and have significant dependencies on other modules. A "utils" module should contain stateless, dependency-free helpers.

**Impact:** Misleading code organization makes navigation difficult; these modules are harder to find and their importance is obscured.

### 3. Provider-Specific Leakage into Tools System

The `tools_system.h` exposes format-specific functions:

```c
// OpenAI format
char* generate_tools_json(const ToolRegistry *registry);
int parse_tool_calls(const char *json_response, ...);

// Anthropic format
char* generate_anthropic_tools_json(const ToolRegistry *registry);
int parse_anthropic_tool_calls(const char *json_response, ...);
```

The tools system shouldn't know about specific LLM provider wire formats. This serialization logic belongs in the respective provider implementations (`anthropic_provider.c`, `openai_provider.c`).

**Impact:** Adding a new provider requires modifying both the provider and the tools system; violates Open-Closed Principle.

### 4. Core Module is a God Object

`RalphSession` aggregates nearly everything:

```c
typedef struct {
    SessionData session_data;
    TodoList todo_list;
    ToolRegistry tools;
    ProviderRegistry provider_registry;
    LLMProvider* provider;
    MCPClient mcp_client;
} RalphSession;
```

The `ralph.c` implementation (~1,600 lines) orchestrates all interaction between components.

**Impact:**
- Difficult to unit test in isolation
- High cognitive load to understand
- Changes to any subsystem risk breaking the coordinator
- Tight coupling makes refactoring risky

### 5. Inconsistent Header Dependencies

Some headers include implementation details they shouldn't need:

| Header | Problematic Include | Issue |
|--------|---------------------|-------|
| `ralph.h` | `http_client.h` | Network concern at application layer |
| `llm_provider.h` | `output_formatter.h` | Presentation concern in LLM layer |

**Impact:** Compilation dependencies are broader than necessary; changes to low-level modules trigger unnecessary recompilation.

### 6. Session vs. Conversation Ambiguity

The `src/session/` module conflates multiple concerns:

| File | Concern |
|------|---------|
| `session_manager.h/c` | User session state |
| `conversation_tracker.h/c` | Message history persistence |
| `conversation_compactor.h/c` | History compression algorithms |
| `token_manager.h/c` | Context window allocation |

"Session" typically means transient user session state. Conversation management (persistence, compression, tokenization) is a distinct domain concern.

**Impact:** Module cohesion is reduced; it's unclear where new conversation-related features should go.

### 7. Tool Module Structure

The `src/tools/` directory mixes three different concerns:

1. **Infrastructure:** `tools_system.c`, `tool_result_builder.c`
2. **Tool Implementations:** `file_tools.c`, `shell_tool.c`, `memory_tool.c`, etc.
3. **Domain Logic:** `todo_manager.c`, `todo_display.c`

The todo manager in particular is domain logic that happens to be exposed as a tool, not tool infrastructure.

**Impact:** Unclear where new functionality belongs; testing tool infrastructure requires navigating around implementations.

---

## Suggested Reorganization

```
src/
├── core/              # Slim application coordinator
│   ├── main.c
│   └── ralph.c        # Reduced to pure orchestration
│
├── cli/               # Command-line interface
│   └── memory_commands.c
│
├── config/            # Unified configuration (merge duplicates)
│   ├── config.c
│   └── env_loader.c
│
├── network/           # HTTP layer
│   ├── http_client.c
│   └── api_common.c
│
├── llm/
│   ├── provider.c     # Provider registry and interface
│   ├── providers/     # Provider implementations (own their serialization)
│   │   ├── anthropic_provider.c
│   │   ├── openai_provider.c
│   │   └── local_ai_provider.c
│   ├── models/        # Model capability definitions
│   └── embeddings/    # Embedding services
│
├── conversation/      # Message history management
│   ├── tracker.c
│   ├── compactor.c
│   └── token_manager.c
│
├── session/           # User session state only
│   └── session_manager.c
│
├── storage/
│   ├── vector/        # Vector DB, HNSW wrapper, service
│   ├── document/      # Document store
│   └── metadata/      # Metadata store
│
├── tools/
│   ├── core/          # Registry, execution engine, result builder
│   │   ├── registry.c
│   │   ├── executor.c
│   │   └── result_builder.c
│   └── impl/          # Individual tool implementations
│       ├── file_tools.c
│       ├── shell_tool.c
│       ├── memory_tool.c
│       ├── vector_db_tool.c
│       ├── pdf_tool.c
│       └── links_tool.c
│
├── ingestion/         # Document processing pipeline
│   ├── pdf_processor.c
│   ├── pdf_extractor.c
│   └── document_chunker.c
│
├── retrieval/         # Context retrieval for prompts
│   ├── context_retriever.c
│   └── prompt_loader.c
│
├── domain/            # Domain-specific logic
│   ├── todo_manager.c
│   └── todo_display.c
│
├── mcp/               # Model Context Protocol (unchanged)
│   └── mcp_client.c
│
└── common/            # True utilities only
    ├── json_escape.c
    ├── common_utils.c
    ├── debug_output.c
    └── output_formatter.c
```

---

## Summary

| Aspect | Current State | Recommendation |
|--------|---------------|----------------|
| **Layering** | Good overall, some violations | Move serialization to providers |
| **Cohesion** | Mixed - utils is a dumping ground | Extract domain logic to proper modules |
| **Coupling** | Provider-specific code leaks into tools | Providers own their wire formats |
| **Extensibility** | Good for providers and tools | Maintain current patterns |
| **Testability** | Challenged by god object in core | Decompose RalphSession responsibilities |
| **Configuration** | Duplicated structures | Consolidate to single source of truth |

---

## Priority Actions

1. **High:** Consolidate `RalphConfig` and `ralph_config_t` into a single configuration system
2. **High:** Move tool JSON serialization (`generate_*_tools_json`, `parse_*_tool_calls`) into provider implementations
3. **Medium:** Extract `pdf_processor`, `context_retriever`, `prompt_loader`, `document_chunker` from utils
4. **Medium:** Split `src/session/` into session state vs. conversation management
5. **Low:** Reorganize `src/tools/` into core infrastructure and implementations
6. **Low:** Decompose `ralph.c` into smaller, focused coordinators

---

*Generated: 2026-01-18*
