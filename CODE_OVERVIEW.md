# Ralph Code Overview

This document provides a comprehensive overview of Ralph's codebase structure and where to find major functionality. For visual architecture diagrams, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Directory Structure

### `src/` - Application Layer

Application-specific code that uses the library layer.

#### `src/core/` - Core Application Logic
- **`main.c`** - Application entry point with CLI argument parsing (--debug, --json, --yolo, --subagent modes)
- **`ralph.c/h`** - Session orchestration: initialization, message processing, LLM API communication, tool workflow execution
- **`async_executor.c/h`** - Non-blocking message processing using background thread with pipe notification
- **`context_enhancement.c/h`** - Prompt enhancement with todo state, memory recall, and context retrieval
- **`recap.c/h`** - Conversation recap generation (one-shot LLM calls without history persistence)
- **`streaming_handler.c/h`** - Application-layer streaming orchestration and provider registry management
- **`tool_executor.c/h`** - Iterative tool-calling state machine for executing tool workflows

#### `src/llm/` - Language Model Integration
- **`llm_provider.c/h`** - LLM provider abstraction layer with registry pattern
- **`model_capabilities.c/h`** - Model capability detection and management (context windows, features)
- **`embeddings.c/h`** - Low-level text embedding functionality
- **`embedding_provider.c/h`** - Embedding provider interface and registry
- **`embeddings_service.c/h`** - Thread-safe embedding service singleton

##### `src/llm/models/` - Model-Specific Implementations
- **`claude_model.c`** - Anthropic Claude model integration (200k context)
- **`gpt_model.c`** - OpenAI GPT/O1/O4 model integration (128k context)
- **`deepseek_model.c`** - DeepSeek model integration with thinking tags
- **`qwen_model.c`** - Qwen model integration with thinking tags
- **`default_model.c`** - Default/fallback model implementation (4k context)
- **`response_processing.c/h`** - Thinking tag processing for model responses

##### `src/llm/providers/` - API Provider Implementations
- **`anthropic_provider.c`** - Anthropic API client (Messages format)
- **`openai_provider.c`** - OpenAI API client (Chat completions)
- **`local_ai_provider.c`** - Local AI server integration (LM Studio, Ollama)
- **`openai_embedding_provider.c`** - OpenAI embedding API provider
- **`local_embedding_provider.c`** - Local embedding service provider

#### `src/network/` - Network Communication
- **`http_client.c/h`** - HTTP client implementation using libcurl (buffered and streaming)
- **`api_common.c/h`** - Common API utilities, JSON payload building for OpenAI/Anthropic formats
- **`streaming.c/h`** - SSE streaming infrastructure for real-time response handling
- **`api_error.c/h`** - Enhanced API error handling with retry logic
- **`embedded_cacert.c/h`** - Embedded Mozilla CA certificate bundle for portable SSL/TLS

#### `src/session/` - User Session Management
- **`session_manager.c/h`** - Session data structures and API payload building
- **`conversation_tracker.c/h`** - Conversation history tracking with vector DB persistence
- **`conversation_compactor.c/h`** - Intelligent conversation history compression
- **`rolling_summary.c/h`** - Rolling conversation summary generation with compaction thresholds
- **`token_manager.c/h`** - Token counting, allocation, and context window management

#### `src/mcp/` - Model Context Protocol
- **`mcp_client.c/h`** - MCP client implementation and server management
- **`mcp_transport.c/h`** - Transport abstraction layer using strategy pattern
- **`mcp_transport_stdio.c`** - STDIO transport for local MCP processes
- **`mcp_transport_http.c`** - HTTP transport for remote MCP servers

#### `src/messaging/` - Inter-Agent Messaging
- **`notification_formatter.c/h`** - Formats messages for LLM context injection

#### `src/db/` - Database Layer
- **`vector_db.c/h`** - Core vector database with HNSWLIB backend
- **`vector_db_service.c/h`** - Thread-safe vector database singleton service
- **`document_store.c/h`** - High-level document storage with embeddings and JSON persistence
- **`metadata_store.c/h`** - Chunk metadata storage layer (separate from vectors)
- **`task_store.c/h`** - SQLite-based persistent task storage with hierarchies and dependencies
- **`hnswlib_wrapper.cpp/h`** - C++ wrapper for HNSW vector indexing

#### `src/policy/` - Approval Gate System

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

#### `src/tools/` - Python Tool Integration
- **`python_tool.c/h`** - Embedded Python interpreter execution
- **`python_tool_files.c/h`** - Python file-based tool loading system

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

#### `src/utils/` - Application Utilities
- **`config.c/h`** - Configuration management with cascading priority (local → user → env → defaults)
- **`prompt_loader.c/h`** - System prompt loading (core + PROMPT.md)
- **`context_retriever.c/h`** - Vector database context retrieval for prompts
- **`pdf_processor.c/h`** - PDF download, extraction, chunking, and indexing pipeline
- **`ralph_home.c/h`** - Centralized Ralph home directory management (~/.local/ralph/)

---

### `lib/` - Reusable Library Layer

Generic, CLI-independent components that can be reused.

#### `lib/util/` - Generic Utilities
- **`darray.h`** - Type-safe dynamic array macros (header-only)
- **`ptrarray.h`** - Type-safe dynamic pointer array with ownership semantics (header-only)
- **`uuid_utils.c/h`** - UUID v4 generation and validation
- **`interrupt.c/h`** - Cooperative Ctrl+C cancellation with signal handling
- **`common_utils.c/h`** - General utility functions (string ops, JSON extraction)
- **`json_escape.c/h`** - JSON string escaping utilities
- **`debug_output.c/h`** - Conditional debug logging with ANSI colors
- **`document_chunker.c/h`** - Intelligent text chunking for embeddings

#### `lib/pdf/` - PDF Processing
- **`pdf_extractor.c/h`** - PDF text extraction using PDFio library

#### `lib/db/` - Database Abstraction
- **`sqlite_dal.c/h`** - SQLite data access layer with mutex protection, schema initialization, and common query patterns

#### `lib/ipc/` - Inter-Process Communication
- **`pipe_notifier.c/h`** - Thread-safe pipe-based notification for async event handling
- **`agent_identity.c/h`** - Thread-safe agent identity management (ID, parent ID, subagent status)
- **`message_store.c/h`** - SQLite-backed inter-agent messaging storage (direct messages, pub/sub channels)
- **`message_poller.c/h`** - Background thread for polling new messages with PipeNotifier integration

#### `lib/ui/` - User Interface Components
- **`terminal.c/h`** - Terminal abstraction with colors, ANSI escapes, and layout helpers
- **`spinner.c/h`** - Pulsing spinner for tool execution visual feedback
- **`output_formatter.c/h`** - Response formatting and display for LLM responses
- **`json_output.c/h`** - JSON output mode for programmatic integration (--json flag)
- **`memory_commands.c/h`** - Interactive `/memory` slash commands for direct memory management
- **`repl.c/h`** - Read-Eval-Print-Loop implementation with readline

#### `lib/tools/` - Tool System
- **`tools_system.c/h`** - Core tool registry and execution framework
- **`tool_result_builder.c/h`** - Standardized tool result construction (builder pattern)
- **`tool_param_dsl.c/h`** - Declarative table-driven tool parameter registration
- **`tool_format.h`** - Provider-specific tool format strategy interface
- **`tool_format_anthropic.c`** - Anthropic Messages API tool format
- **`tool_format_openai.c`** - OpenAI Chat Completions tool format
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

#### `lib/agent/` - Agent Abstraction
- **`agent.c/h`** - Agent lifecycle management, mode selection (interactive/background), signal handling

#### `lib/services/` - Service Container
- **`services.c/h`** - Dependency injection container for service management

#### `lib/workflow/` - Task Queue
- **`workflow.c/h`** - SQLite-backed work queue for asynchronous task processing

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

#### `test/llm/` - LLM System Tests
- **`test_model_tools.c`** - Model and tool integration tests

#### `test/network/` - Network Layer Tests
- **`test_http_client.c`** - HTTP client functionality tests
- **`test_messages_array_bug.c`** - Message handling regression tests
- **`test_streaming.c`** - Streaming infrastructure tests
- **`test_anthropic_streaming.c`** & **`test_openai_streaming.c`** - Provider streaming tests
- **`test_http_retry.c`** - HTTP retry logic tests

#### `test/session/` - Session Management Tests
- **`test_conversation_tracker.c`** - Conversation persistence tests
- **`test_conversation_compactor.c`** - History compression tests
- **`test_token_manager.c`** - Token management tests
- **`test_rolling_summary.c`** - Rolling summary generation tests

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
- **`test_ralph_home.c`** - Ralph home directory management tests
- **`test_pipe_notifier.c`** - Pipe notifier async notification tests
- **`test_spinner.c`** - Tool execution spinner tests

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
- **`mock_embeddings.c`** - Mock embeddings for testing
- **`test/stubs/`** - Test stubs for isolated unit testing

---

## Key Architectural Components

### 1. Multi-Provider LLM System
The `src/llm/` directory implements a flexible provider system using the registry pattern:
- **Provider Registry**: URL-based detection for Anthropic, OpenAI, and local servers
- **Model Registry**: Pattern-based model capability detection
- **Embedding Provider Registry**: Parallel system for embedding APIs

### 2. Tool System
The `lib/tools/` directory implements a safe, extensible tool execution framework:
- **Tool Registry**: Dynamic tool registration and JSON-based execution
- **Result Builder**: Standardized response formatting
- **Python Tools**: File ops, shell, web fetch loaded from Python files in `src/tools/python_defaults/`
- **Native Tools**: Memory, todos, PDFs, vector DB, subagents implemented in C
- **Extensibility**: Custom Python tools can be added to `~/.local/ralph/tools/`

### 3. Vector Database Integration
The `src/db/` directory provides a layered persistence architecture:
- **Vector DB**: HNSWLIB-based similarity search with thread-safe access
- **Document Store**: High-level document management with embedding integration
- **Metadata Store**: Separate JSON persistence for chunk metadata

### 4. Session Management
The `src/session/` directory handles conversation lifecycle:
- **Conversation Tracker**: History persistence with vector DB integration
- **Token Manager**: Dynamic token allocation and context trimming
- **Compactor**: Intelligent conversation compression preserving tool sequences

### 5. Approval Gate System
The `src/policy/` directory implements security-aware tool execution control:
- **Category-Based Gates**: Tools categorized as file_read, file_write, shell, network, memory, subagent, mcp, python
- **Allowlist Matching**: Regex patterns for paths/URLs, shell command prefix matching
- **Protected Files**: Hard-block access to config files via basename, glob, and inode detection
- **Rate Limiting**: Exponential backoff after repeated denials
- **Subagent Proxy**: IPC-based approval forwarding for child processes

### 6. MCP Protocol Integration
The `src/mcp/` directory provides Model Context Protocol support:
- **Multi-Transport**: STDIO (local processes), HTTP, and SSE connections
- **Dynamic Discovery**: Tools fetched via JSON-RPC at connection time
- **Namespaced Tools**: Registered as `mcp_{servername}_{toolname}`

### 7. Configuration System
Centralized configuration in `src/utils/config.c`:
- **Priority**: Local `ralph.config.json` → User config → Environment → Defaults
- **Auto-Generation**: Creates config from environment variables
- **MCP Config**: Server definitions in `mcpServers` section

---

## Development Workflow

1. **Core Logic**: Start with `src/core/main.c` for application flow
2. **Tool Development**: Add new tools in `lib/tools/` with corresponding tests
3. **LLM Integration**: Extend providers in `src/llm/providers/` or models in `src/llm/models/`
4. **Utilities**: Generic utilities go in `lib/util/`, application-specific in `src/utils/`
5. **Testing**: Every module has corresponding tests in the `test/` directory structure

## Build System

- **Main Build**: `./scripts/build.sh` - Builds the complete application
- **Testing**: `./scripts/run_tests.sh` - Runs the full test suite
- **Memory Check**: `make check-valgrind` - Runs Valgrind memory analysis
- **Clean**: `./scripts/build.sh clean` - Removes build artifacts
