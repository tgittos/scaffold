# Ralph Code Overview

This document provides a comprehensive overview of Ralph's codebase structure and where to find major functionality. For visual architecture diagrams, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Directory Structure

### `src/` - Application Layer

Application-specific code that uses the library layer. This is a thin wrapper around lib/.

#### `src/ralph/` - Ralph CLI (Standalone Agent)
- **`main.c`** - Application entry point with CLI argument parsing (--debug, --json, --yolo, --subagent modes). Thin wrapper that invokes lib/agent/agent.h API.

#### `src/scaffold/` - Scaffold CLI (Orchestrator)
- **`main.c`** - Orchestrator entry point. Supports REPL, one-shot, --supervisor --goal, --worker --queue modes. Coordinates goal decomposition and supervisor spawning.

#### `src/tools/` - Python Tool Integration
- **`python_tool.c/h`** - Embedded Python interpreter execution
- **`python_tool_files.c/h`** - Python file-based tool loading system
- **`python_extension.c/h`** - Tool extension interface for Python tools integration with lib/

##### Python Default Tools (in `python_defaults/`)
- **`read_file.py`** - Read file contents
- **`write_file.py`** - Write content to file
- **`append_file.py`** - Append content to file
- **`file_info.py`** - Get file metadata
- **`list_dir.py`** - List directory contents with filtering
- **`search_files.py`** - Search files by content or pattern
- **`apply_delta.py`** - Apply unified diff patches
- **`shell.py`** - Shell command execution with timeout
- **`web_fetch.py`** - Fetch and process web content

---

### `lib/` - Reusable Library Layer

Generic, CLI-independent components that can be reused. The ralph CLI is a thin wrapper around this library.

#### `lib/agent/` - Agent Abstraction and Session Management
- **`agent.c/h`** - Agent lifecycle management, mode selection (interactive/background/worker), configuration
- **`session.c/h`** - Thin coordinator delegating to extracted modules below
- **`session_configurator.c/h`** - Configuration loading: API settings, type detection, system prompt, context window
- **`message_dispatcher.c/h`** - Dispatch path selection (streaming vs buffered) based on provider capabilities
- **`message_processor.c/h`** - Buffered (non-streaming) response handling and tool call extraction
- **`api_round_trip.c/h`** - Single LLM request-response cycle (payload build, HTTP, parse)
- **`conversation_state.c/h`** - Conversation history append helpers (assistant messages, tool results)
- **`tool_batch_executor.c/h`** - Batch tool execution with approval gate integration
- **`tool_orchestration.c/h`** - Tool call deduplication and per-batch tracking
- **`repl.c/h`** - Read-Eval-Print-Loop for interactive mode with readline and async processing
- **`async_executor.c/h`** - Non-blocking message processing using background thread with pipe notification
- **`context_enhancement.c/h`** - Prompt enhancement with todo state, memory recall, and context retrieval
- **`recap.c/h`** - Conversation recap generation (one-shot LLM calls without history persistence)
- **`streaming_handler.c/h`** - Application-layer streaming orchestration and provider registry management
- **`tool_executor.c/h`** - Thin entry point for tool execution workflow (init, initial batch, hand-off)
- **`iterative_loop.c/h`** - Iterative tool-calling loop for follow-up LLM rounds

#### `lib/session/` - Session Data Management
- **`session_manager.c/h`** - Session data structures (SessionData with config, conversation, model info)
- **`conversation_tracker.c/h`** - Conversation history tracking with vector DB persistence
- **`conversation_compactor.c/h`** - Intelligent conversation history compression
- **`rolling_summary.c/h`** - Rolling conversation summary generation with compaction thresholds
- **`token_manager.c/h`** - Token counting, allocation, and context window management

#### `lib/db/` - Database Layer
- **`vector_db.c/h`** - Core vector database with HNSWLIB backend
- **`vector_db_service.c/h`** - Thread-safe vector database singleton service
- **`document_store.c/h`** - High-level document storage with embeddings and JSON persistence
- **`metadata_store.c/h`** - Chunk metadata storage layer (separate from vectors)
- **`task_store.c/h`** - SQLite-based persistent task storage with hierarchies and dependencies
- **`goal_store.c/h`** - GOAP goal persistence: goal state, world state, supervisor PID tracking
- **`action_store.c/h`** - GOAP action persistence: hierarchy (compound/primitive), preconditions/effects, readiness queries
- **`sqlite_dal.c/h`** - SQLite data access layer with mutex protection, schema initialization, and common query patterns
- **`hnswlib_wrapper.cpp/h`** - C++ wrapper for HNSW vector indexing

#### `lib/llm/` - LLM Core Framework
- **`llm_provider.c/h`** - LLM provider abstraction layer with registry pattern for API providers
- **`model_capabilities.c/h`** - Model capability detection and management (context windows, features)
- **`embeddings.c/h`** - Low-level text embedding functionality
- **`embedding_provider.c/h`** - Embedding provider interface and registry
- **`embeddings_service.c/h`** - Thread-safe embedding service singleton
- **`llm_client.c/h`** - LLM HTTP client abstraction wrapping http_client for API calls

##### `lib/llm/models/` - Model Capability Implementations
- **`model_registry.c`** - Data-driven model registration (all models in one static table)
- **`claude_model.c`** - Claude assistant tool message formatting
- **`gpt_model.c`** - GPT assistant tool message formatting
- **`response_processing.c/h`** - Thinking tag processing for reasoning models

##### `lib/llm/providers/` - API Provider Implementations
- **`anthropic_provider.c`** - Anthropic API client (Messages format)
- **`openai_provider.c`** - OpenAI API client (Chat completions)
- **`local_ai_provider.c`** - Local AI server integration (LM Studio, Ollama)
- **`openai_embedding_provider.c`** - OpenAI embedding API provider
- **`local_embedding_provider.c`** - Local embedding service provider

#### `lib/network/` - Network Communication
- **`http_client.c/h`** - HTTP client implementation using libcurl (buffered and streaming)
- **`api_common.c/h`** - Common API utilities, JSON payload building for OpenAI/Anthropic formats
- **`streaming.c/h`** - SSE streaming infrastructure for real-time response handling
- **`api_error.c/h`** - Enhanced API error handling with retry logic
- **`embedded_cacert.c/h`** - Embedded Mozilla CA certificate bundle for portable SSL/TLS

#### `lib/policy/` - Approval Gate System

##### Core Components
- **`approval_gate.c/h`** - Core orchestration: category lookup, gate checking, config management
- **`rate_limiter.c/h`** - Opaque rate limiter type with exponential backoff tracking
- **`allowlist.c/h`** - Opaque allowlist type for regex and shell pattern matching
- **`gate_prompter.c/h`** - Terminal UI for single and batch approval prompts
- **`pattern_generator.c/h`** - Pure functions for generating allowlist patterns
- **`tool_args.c/h`** - Centralized cJSON argument extraction from ToolCall structs

##### Protected Files
- **`protected_files.c/h`** - Protected file detection via basename, prefix, glob, and inode matching
- **`path_normalize.c/h`** - Cross-platform path normalization (Windows drive letters, UNC paths)

##### Shell Parsing
- **`shell_parser.c/h`** - POSIX shell tokenization and dangerous pattern detection
- **`shell_parser_cmd.c`** - cmd.exe parsing with metacharacter detection
- **`shell_parser_ps.c`** - PowerShell parsing with cmdlet detection

##### TOCTOU Protection
- **`atomic_file.c/h`** - Atomic file operations with O_NOFOLLOW, O_EXCL, inode verification
- **`verified_file_context.c/h`** - Thread-local storage for approved path context
- **`verified_file_python.c/h`** - Python extension for TOCTOU-safe file operations

##### Subagent Support
- **`subagent_approval.c/h`** - IPC-based approval proxying for child processes
- **`approval_errors.c`** - JSON error message formatting for approval gate denials

#### `lib/tools/` - Tool System
- **`tools_system.c/h`** - Core tool registry and execution framework
- **`tools.h`** - Public tools API header
- **`tool_result_builder.c/h`** - Standardized tool result construction (builder pattern)
- **`tool_param_dsl.c/h`** - Declarative table-driven tool parameter registration
- **`tool_format.h`** - Provider-specific tool format strategy interface
- **`tool_format_anthropic.c`** - Anthropic Messages API tool format
- **`tool_format_openai.c`** - OpenAI Chat Completions tool format
- **`tool_extension.c/h`** - Extension interface for external tool systems (e.g., Python)
- **`builtin_tools.c/h`** - Registration of built-in tools
- **`memory_tool.c/h`** - Persistent semantic memory (remember, recall_memories, forget_memory)
- **`pdf_tool.c/h`** - PDF processing and vector indexing
- **`vector_db_tool.c/h`** - Vector database operations (13 tools)
- **`subagent_tool.c/h`** - Subagent process spawning and management
- **`subagent_process.c/h`** - Subagent process I/O and lifecycle management
- **`messaging_tool.c/h`** - Inter-agent messaging (6 tools)
- **`todo_manager.c/h`** - Todo data structures and operations
- **`todo_tool.c/h`** - Todo tool call handler
- **`todo_display.c/h`** - Todo console visualization
- **`goap_tools.c/h`** - GOAP goal/action tools for supervisors (9 tools, scaffold mode only)
- **`orchestrator_tool.c/h`** - Orchestrator lifecycle tools: execute_plan, list_goals, goal_status, start/pause/cancel_goal (scaffold mode only)

#### `lib/ipc/` - Inter-Process Communication
- **`ipc.h`** - Public IPC API header
- **`pipe_notifier.c/h`** - Thread-safe pipe-based notification for async event handling
- **`agent_identity.c/h`** - Thread-safe agent identity management (ID, parent ID, subagent status)
- **`message_store.c/h`** - SQLite-backed inter-agent messaging storage (direct messages, pub/sub channels)
- **`message_poller.c/h`** - Background thread for polling new messages with PipeNotifier integration
- **`notification_formatter.c/h`** - Formats messages for LLM context injection

#### `lib/ui/` - User Interface Components
- **`ui.h`** - Public UI API header
- **`output.h`** - Output formatting header
- **`terminal.c/h`** - Terminal abstraction with colors, ANSI escapes, and layout helpers
- **`spinner.c/h`** - Pulsing spinner for tool execution visual feedback
- **`output_formatter.c/h`** - Response formatting and display for LLM responses
- **`json_output.c/h`** - JSON output mode for programmatic integration (--json flag)
- **`slash_commands.c/h`** - Slash command registry and dispatch (`/help`, conditional scaffold commands)
- **`memory_commands.c/h`** - Interactive `/memory` slash commands for direct memory management
- **`task_commands.c/h`** - `/tasks` command for viewing task store entries
- **`agent_commands.c/h`** - `/agents` command for subagent and supervisor status
- **`goal_commands.c/h`** - `/goals` command for GOAP goal listing and action tree display (scaffold only)
- **`model_commands.c/h`** - `/model` command for switching AI models
- **`mode_commands.c/h`** - `/mode` command for switching behavioral prompt modes

#### `lib/util/` - Generic Utilities
- **`darray.h`** - Type-safe dynamic array macros (header-only)
- **`ptrarray.h`** - Type-safe dynamic pointer array with ownership semantics (header-only)
- **`uuid_utils.c/h`** - UUID v4 generation and validation
- **`interrupt.c/h`** - Cooperative Ctrl+C cancellation with signal handling
- **`common_utils.c/h`** - General utility functions (string ops, JSON extraction)
- **`json_escape.c/h`** - JSON string escaping utilities
- **`debug_output.c/h`** - Conditional debug logging with ANSI colors
- **`document_chunker.c/h`** - Intelligent text chunking for embeddings
- **`config.c/h`** - Configuration management with cascading priority (local -> user -> env -> defaults)
- **`prompt_loader.c/h`** - System prompt loading (core + AGENTS.md)
- **`context_retriever.c/h`** - Vector database context retrieval for prompts
- **`ansi_codes.h`** - Terminal color codes, box-drawing characters, and status symbols
- **`app_home.c/h`** - Centralized application home directory management (~/.local/<app_name>/)

#### `lib/pdf/` - PDF Processing
- **`pdf_extractor.c/h`** - PDF text extraction using PDFio library

#### `lib/mcp/` - Model Context Protocol
- **`mcp_client.c/h`** - MCP client implementation and server management
- **`mcp_transport.c/h`** - Transport abstraction layer using strategy pattern
- **`mcp_transport_stdio.c`** - STDIO transport for local MCP processes
- **`mcp_transport_http.c`** - HTTP transport for remote MCP servers

#### `lib/services/` - Service Container
- **`services.c/h`** - Dependency injection container for service management (message store, vector DB, embeddings, task store)

#### `lib/orchestrator/` - Scaffold Orchestration
- **`supervisor.c/h`** - Supervisor event loop: GOAP tool-driven goal progression, message-poller-based wake-on-worker-completion
- **`orchestrator.c/h`** - Supervisor spawning (fork/exec), liveness checks, zombie reaping, stale PID cleanup, respawn

#### `lib/workflow/` - Task Queue
- **`workflow.c/h`** - SQLite-backed work queue for asynchronous task processing and worker management

#### Top-Level Library Headers
- **`lib/libagent.h`** - Public API for the agent library (includes all major subsystems)
- **`lib/types.h`** - Shared types (ToolCall, ToolResult, StreamingToolUse)

---

### `test/` - Test Suite

The test directory mirrors the source structure:

#### `test/core/` - Core Functionality Tests
- **`test_main.c`** - Main application tests
- **`test_ralph.c`** - Core Ralph functionality tests
- **`test_cli_flags.c`** - CLI flag parsing tests
- **`test_recap.c`** - Recap generation tests
- **`test_async_executor.c`** - Async executor background thread tests
- **`test_interrupt.c`** - Cooperative Ctrl+C cancellation tests
- **`test_agent_identity.c`** - Agent identity thread-safety tests
- **`test_incomplete_task_bug.c`** - Incomplete task handling regression tests
- **`test_message_dispatcher.c`** - Dispatch path selection tests
- **`test_message_processor.c`** - Buffered response processing tests

#### `test/llm/` - LLM System Tests
- **`test_model_tools.c`** - Model and tool integration tests
- **`test_anthropic_streaming.c`** - Anthropic provider streaming tests
- **`test_openai_streaming.c`** - OpenAI provider streaming tests

#### `test/network/` - Network Layer Tests
- **`test_http_client.c`** - HTTP client functionality tests
- **`test_messages_array_bug.c`** - Message handling regression tests
- **`test_streaming.c`** - SSE streaming infrastructure tests
- **`test_http_retry.c`** - HTTP retry logic tests

#### `test/session/` - Session Management Tests
- **`test_conversation_tracker.c`** - Conversation persistence tests
- **`test_conversation_compactor.c`** - History compression tests
- **`test_token_manager.c`** - Token management tests
- **`test_rolling_summary.c`** - Rolling summary generation tests
- **`test_conversation_vector_db.c`** - Conversation vector DB integration tests
- **`test_tool_calls_not_stored.c`** - Tool call storage filtering tests

#### `test/tools/` - Tool System Tests
- **`test_tools_system.c`** - Core tool system tests
- **`test_memory_tool.c`** - Memory system tests
- **`test_todo_manager.c`** - Task management tests
- **`test_vector_db_tool.c`** - Vector database tests
- **`test_python_tool.c`** & **`test_python_integration.c`** - Python interpreter tests
- **`test_subagent_tool.c`** - Subagent system tests
- **`test_tool_param_dsl.c`** - Tool parameter DSL tests

#### `test/pdf/` - PDF Processing Tests
- **`test_pdf_extractor.c`** - PDF extraction functionality tests

#### `test/db/` - Database Tests
- **`test_vector_db.c`** - Vector database core tests
- **`test_document_store.c`** - Document store tests
- **`test_task_store.c`** - Task store persistence tests
- **`test_message_store.c`** - Inter-agent messaging storage tests
- **`test_sqlite_dal.c`** - SQLite Data Access Layer tests

#### `test/mcp/` - MCP Integration Tests
- **`test_mcp_client.c`** - MCP client functionality tests

#### `test/messaging/` - Messaging Tests
- **`test_message_poller.c`** - Message poller background thread tests
- **`test_notification_formatter.c`** - Notification formatting tests

#### `test/utils/` - Utility Tests
- **`test_output_formatter.c`** - Output formatting tests
- **`test_prompt_loader.c`** - Prompt loading tests
- **`test_config.c`** - Configuration system tests
- **`test_json_output.c`** - JSON output mode tests
- **`test_debug_output.c`** - Debug output tests
- **`test_app_home.c`** - Application home directory management tests
- **`test_pipe_notifier.c`** - Pipe notifier async notification tests
- **`test_spinner.c`** - Tool execution spinner tests
- **`test_terminal.c`** - Terminal abstraction tests

#### `test/policy/` - Policy Tests (Approval Gates)
- **`test_approval_gate.c`** - Gate config initialization, category lookup, non-interactive mode
- **`test_approval_gate_integration.c`** - End-to-end approval flow tests
- **`test_rate_limiter.c`** - Denial tracking, exponential backoff, reset behavior
- **`test_allowlist.c`** - Regex and shell pattern matching, JSON loading
- **`test_gate_prompter.c`** - Gate prompter terminal UI tests
- **`test_shell_parser.c`** - POSIX shell tokenization and dangerous pattern detection
- **`test_shell_parser_cmd.c`** - cmd.exe parsing tests
- **`test_shell_parser_ps.c`** - PowerShell parsing and dangerous cmdlet detection
- **`test_path_normalize.c`** - Cross-platform path normalization
- **`test_protected_files.c`** - Protected file detection
- **`test_atomic_file.c`** - TOCTOU-safe file operations
- **`test_subagent_approval.c`** - Approval proxy pipe management

#### `test/` - Root-Level Tests
- **`test_darray.c`** - Dynamic array implementation tests
- **`test_ptrarray.c`** - Pointer array implementation tests
- **`test_document_chunker.c`** - Document chunking algorithm tests
- **`test_memory_management.c`** - Memory management tests
- **`test_tool_args.c`** - Tool argument extraction tests
- **`test_verified_file_context.c`** - Verified file context tests

#### Test Infrastructure
- **`test/unity/`** - Unity testing framework (vendored)
- **`mock_api_server.c/h`** - Mock API server for testing
- **`mock_embeddings.c/h`** - Mock embeddings for deterministic vector testing
- **`mock_embeddings_server.c/h`** - Mock embeddings HTTP server
- **`mock_embeddings_service.c/h`** - Mock embeddings service for DI
- **`test/stubs/`** - Test stubs for isolated unit testing (ralph_stub, services_stub, output_formatter_stub, python_tool_stub, subagent_stub)

---

## Key Architectural Components

### 1. Library-First Architecture
The codebase follows a library-first design where all core functionality lives in `lib/`:
- **libagent.h**: Public API header that exposes the entire library
- **src/ralph/main.c**: Thin CLI wrapper that parses arguments and invokes lib/agent/agent.h
- This enables embedding ralph functionality in other programs

### 2. Agent Abstraction (lib/agent/)
The agent module provides a clean lifecycle API:
- **Agent**: High-level wrapper with mode-based execution (interactive, single-shot, background, worker)
- **AgentSession**: Aggregates all components (tools, MCP, subagents, approval gates, message polling)
- **AgentConfig**: Configuration struct with dependency injection support

### 3. Multi-Provider LLM System (lib/llm/)
Flexible provider system using the registry pattern:
- **Provider Registry**: URL-based detection for Anthropic, OpenAI, and local servers
- **Model Registry**: Pattern-based model capability detection
- **Embedding Provider Registry**: Parallel system for embedding APIs

### 4. Tool System (lib/tools/)
Safe, extensible tool execution framework:
- **Tool Registry**: Dynamic tool registration and JSON-based execution
- **Tool Extensions**: Plugin system for external tools (e.g., Python from src/)
- **Result Builder**: Standardized response formatting
- **Native Tools**: Memory, todos, PDFs, vector DB, subagents, messaging

### 5. Vector Database Integration (lib/db/)
Layered persistence architecture:
- **Vector DB**: HNSWLIB-based similarity search with thread-safe access
- **Document Store**: High-level document management with embedding integration
- **Metadata Store**: Separate JSON persistence for chunk metadata

### 6. Session Management (lib/session/)
Conversation lifecycle handling:
- **Conversation Tracker**: History persistence with vector DB integration
- **Token Manager**: Dynamic token allocation and context trimming
- **Compactor**: Intelligent conversation compression preserving tool sequences

### 7. Approval Gate System (lib/policy/)
Security-aware tool execution control:
- **Category-Based Gates**: Tools categorized as file_read, file_write, shell, network, memory, subagent, mcp, python
- **Allowlist Matching**: Regex patterns for paths/URLs, shell command prefix matching
- **Protected Files**: Hard-block access to config files via basename, glob, and inode detection
- **Rate Limiting**: Exponential backoff after repeated denials
- **Subagent Proxy**: IPC-based approval forwarding for child processes

### 8. Service Container (lib/services/)
Dependency injection for testability:
- **Services**: Container holding message store, vector DB, embeddings, task store
- Supports both singleton instances and custom injected services

### 9. MCP Protocol Integration (lib/mcp/)
Model Context Protocol support:
- **Multi-Transport**: STDIO (local processes), HTTP, and SSE connections
- **Dynamic Discovery**: Tools fetched via JSON-RPC at connection time
- **Namespaced Tools**: Registered as `mcp_{servername}_{toolname}`

### 10. Configuration System (lib/util/config.c)
Centralized configuration:
- **Priority**: Local `ralph.config.json` -> User config -> Environment -> Defaults
- **Auto-Generation**: Creates config from environment variables
- **MCP Config**: Server definitions in `mcpServers` section

---

## Development Workflow

1. **Core Logic**: Start with `lib/agent/agent.h` for agent lifecycle
2. **CLI Integration**: See `src/ralph/main.c` for argument parsing
3. **Tool Development**: Add new tools in `lib/tools/` with corresponding tests
4. **LLM Integration**: Extend providers in `lib/llm/providers/` or models in `lib/llm/models/`
5. **Python Tools**: Add to `src/tools/python_defaults/` or user's `~/.local/ralph/tools/`
6. **Testing**: Every module has corresponding tests in the `test/` directory structure

## Build System

- **Main Build**: `./scripts/build.sh` - Builds the complete application
- **Testing**: `./scripts/run_tests.sh` - Runs the full test suite
- **Memory Check**: `make check-valgrind` - Runs Valgrind memory analysis
- **Clean**: `./scripts/build.sh clean` - Removes build artifacts
