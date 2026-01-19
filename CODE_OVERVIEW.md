# Ralph Code Architecture Overview

This document provides a comprehensive overview of Ralph's codebase structure and where to find major functionality. For visual architecture diagrams, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Directory Structure

### `src/` - Main Source Code

#### `src/core/` - Core Application Logic
- **`main.c`** - Application entry point with CLI interface (single message and interactive modes)
- **`ralph.c/ralph.h`** - Core Ralph orchestration: session management, API communication, tool execution workflow

#### `src/cli/` - Command Line Interface
- **`memory_commands.c/h`** - Interactive `/memory` slash commands for direct memory management

#### `src/llm/` - Language Model Integration
- **`llm_provider.c/h`** - LLM provider abstraction layer with registry pattern
- **`model_capabilities.c/h`** - Model capability detection and management (context windows, features)
- **`embeddings.c/h`** - Low-level text embedding functionality
- **`embedding_provider.c/h`** - Embedding provider interface and registry (parallel to LLM provider)
- **`embeddings_service.c/h`** - Thread-safe embedding service singleton

##### `src/llm/models/` - Model-Specific Implementations
- **`claude_model.c`** - Anthropic Claude model integration (200k context)
- **`gpt_model.c`** - OpenAI GPT/O1/O4 model integration (128k context)
- **`deepseek_model.c`** - DeepSeek model integration with thinking tags
- **`qwen_model.c`** - Qwen model integration with thinking tags
- **`default_model.c`** - Default/fallback model implementation (4k context)

##### `src/llm/providers/` - API Provider Implementations
- **`anthropic_provider.c`** - Anthropic API client (Messages format)
- **`openai_provider.c`** - OpenAI API client (Chat completions)
- **`local_ai_provider.c`** - Local AI server integration (LM Studio, Ollama)
- **`openai_embedding_provider.c`** - OpenAI embedding API provider
- **`local_embedding_provider.c`** - Local embedding service provider (LMStudio, Ollama)

#### `src/network/` - Network Communication
- **`http_client.c/h`** - HTTP client implementation using libcurl
- **`api_common.c/h`** - Common API utilities, JSON payload building for OpenAI/Anthropic formats

#### `src/session/` - User Session Management
- **`session_manager.c/h`** - Session data structures and API payload building
- **`conversation_tracker.c/h`** - Conversation history tracking with vector DB persistence
- **`conversation_compactor.c/h`** - Intelligent conversation history compression
- **`token_manager.c/h`** - Token counting, allocation, and context window management

#### `src/mcp/` - Model Context Protocol
- **`mcp_client.c/h`** - MCP client implementation supporting STDIO/HTTP/SSE transports

#### `src/tools/` - AI Tool System
- **`tools_system.c/h`** - Core tool registry and execution framework
- **`tool_result_builder.c/h`** - Standardized tool result construction (builder pattern)

##### Individual Tools
- **`file_tools.c/h`** - File system operations (read, write, search, list, info, delta)
- **`shell_tool.c/h`** - Shell command execution with timeout support
- **`memory_tool.c/h`** - Persistent semantic memory (remember, recall_memories, forget)
- **`todo_manager.c/h`** - Todo data structures and operations
- **`todo_tool.c/h`** - Todo tool call handler
- **`todo_display.c/h`** - Todo console visualization
- **`pdf_tool.c/h`** - PDF processing and vector indexing
- **`vector_db_tool.c/h`** - Vector database operations (11 tools)
- **`links_tool.c/h`** - Web content fetching with embedded Links browser

#### `src/db/` - Database Layer
- **`vector_db.c/h`** - Core vector database with HNSWLIB backend
- **`vector_db_service.c/h`** - Thread-safe vector database singleton service
- **`document_store.c/h`** - High-level document storage with embeddings and JSON persistence
- **`metadata_store.c/h`** - Chunk metadata storage layer (separate from vectors)
- **`hnswlib_wrapper.cpp/h`** - C++ wrapper for HNSW vector indexing

#### `src/pdf/` - PDF Processing
- **`pdf_extractor.c/h`** - PDF text extraction using PDFio library

#### `src/utils/` - Utility Functions
- **`config.c/h`** - Configuration management with cascading priority (local → user → env → defaults)
- **`common_utils.c/h`** - General utility functions (string ops, JSON extraction)
- **`env_loader.c/h`** - Environment variable and .env file loading
- **`json_escape.c/h`** - JSON string escaping utilities
- **`output_formatter.c/h`** - Response formatting and display
- **`debug_output.c/h`** - Conditional debug logging with ANSI colors
- **`prompt_loader.c/h`** - System prompt loading (core + PROMPT.md)
- **`context_retriever.c/h`** - Vector database context retrieval for prompts
- **`document_chunker.c/h`** - Intelligent text chunking for embeddings
- **`pdf_processor.c/h`** - PDF download, extraction, chunking, and indexing pipeline

---

### `test/` - Test Suite

The test directory mirrors the source structure:

#### `test/core/` - Core Functionality Tests
- **`test_main.c`** - Main application tests
- **`test_ralph.c`** - Core Ralph functionality tests
- **`test_ralph_integration.c`** - Integration tests

#### `test/llm/` - LLM System Tests
- **`test_model_tools.c`** - Model and tool integration tests

#### `test/network/` - Network Layer Tests
- **`test_http_client.c`** - HTTP client functionality tests
- **`test_messages_array_bug.c`** - Message handling regression tests
- **`test_null_message_fields.c`** - Null field handling tests

#### `test/session/` - Session Management Tests
- **`test_conversation_tracker.c`** - Conversation persistence tests
- **`test_conversation_compactor.c`** - History compression tests
- **`test_token_manager.c`** - Token management tests
- **`test_conversation_loading.c`** - Conversation loading tests

#### `test/tools/` - Tool System Tests
- **`test_tools_system.c`** - Core tool system tests
- **`test_file_tools.c`** - File operation tests
- **`test_shell_tool.c`** - Shell execution tests
- **`test_memory_tool.c`** - Memory system tests
- **`test_todo_manager.c`** & **`test_todo_tool.c`** - Task management tests
- **`test_vector_db_tool.c`** - Vector database tests

#### `test/pdf/` - PDF Processing Tests
- **`test_pdf_extractor.c`** - PDF extraction functionality tests

#### `test/db/` - Database Tests
- **`test_vector_db.c`** - Vector database core tests

#### `test/mcp/` - MCP Integration Tests
- **`test_mcp_client.c`** - MCP client functionality tests
- **`test_mcp_integration.c`** - MCP protocol integration tests

#### `test/utils/` - Utility Tests
- **`test_env_loader.c`** - Configuration loading tests
- **`test_output_formatter.c`** - Output formatting tests
- **`test_prompt_loader.c`** - Prompt loading tests

#### Test Infrastructure
- **`test/unity/`** - Unity testing framework (vendored)
- **`mock_api_server.c/h`** - Mock API server for testing

---

## Key Architectural Components

### 1. Multi-Provider LLM System
The `src/llm/` directory implements a flexible provider system using the registry pattern:
- **Provider Registry**: URL-based detection for Anthropic, OpenAI, and local servers
- **Model Registry**: Pattern-based model capability detection
- **Embedding Provider Registry**: Parallel system for embedding APIs

### 2. Comprehensive Tool System
The `src/tools/` directory implements a safe, extensible tool execution framework:
- **Tool Registry**: Dynamic tool registration and JSON-based execution
- **Result Builder**: Standardized response formatting
- **Categories**: File ops, shell, memory, todos, PDFs, vector DB, web fetch

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

### 5. Network Abstraction
The `src/network/` directory provides HTTP communication:
- **HTTP Client**: libcurl wrapper with configurable timeouts
- **API Common**: JSON payload building for OpenAI/Anthropic message formats

### 6. PDF Processing Pipeline
PDF processing spans multiple directories:
- **`src/pdf/`**: PDFio-based text extraction
- **`src/utils/pdf_processor.c`**: Download, chunk, embed, and index pipeline
- **`src/tools/pdf_tool.c`**: Tool interface for LLM access

### 7. MCP Protocol Integration
The `src/mcp/` directory provides Model Context Protocol support:
- **Multi-Transport**: STDIO (local processes), HTTP, and SSE connections
- **Dynamic Discovery**: Tools fetched via JSON-RPC at connection time
- **Namespaced Tools**: Registered as `mcp_{servername}_{toolname}`

### 8. Configuration System
Centralized configuration in `src/utils/config.c`:
- **Priority**: Local `ralph.config.json` → User config → Environment → Defaults
- **Auto-Generation**: Creates config from environment variables
- **MCP Config**: Server definitions in `mcpServers` section

---

## Unused Directories

The following directories exist but contain no source code (placeholders/build artifacts only):
- `src/http/` - HTTP code is in `src/network/` instead
- `src/daemon/` - Placeholder, not implemented
- `src/server/` - Placeholder, not implemented

---

## Development Workflow

1. **Core Logic**: Start with `src/core/main.c` for application flow
2. **Tool Development**: Add new tools in `src/tools/` with corresponding tests
3. **LLM Integration**: Extend providers in `src/llm/providers/` or models in `src/llm/models/`
4. **Utilities**: Common functionality goes in `src/utils/`
5. **Testing**: Every module has corresponding tests in the `test/` directory structure

## Build System

- **Main Build**: `make` - Builds the complete application
- **Testing**: `make test` - Runs the full test suite
- **Memory Check**: `make check-valgrind` - Runs Valgrind memory analysis
- **Clean**: `make clean` - Removes build artifacts

---

This architecture enables Ralph to be a portable, powerful AI development companion with persistent memory, multi-provider support, MCP integration, and comprehensive tool capabilities.
