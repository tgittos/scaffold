# Ralph Code Architecture Overview

This document provides a comprehensive overview of Ralph's codebase structure and where to find major functionality.

## Directory Structure

### `src/` - Main Source Code

#### `src/core/` - Core Application Logic
- **`main.c`** - Application entry point and main program flow
- **`ralph.c/ralph.h`** - Core Ralph functionality and primary interfaces

#### `src/llm/` - Language Model Integration
- **`llm_provider.c/h`** - Main LLM provider abstraction layer
- **`model_capabilities.c/h`** - Model capability detection and management
- **`embeddings.c/h`** - Text embedding functionality
- **`embeddings_service.c/h`** - Embedding service orchestration

##### `src/llm/models/` - Model-Specific Implementations
- **`claude_model.c`** - Anthropic Claude model integration
- **`gpt_model.c`** - OpenAI GPT model integration
- **`deepseek_model.c`** - DeepSeek model integration
- **`qwen_model.c`** - Qwen model integration
- **`default_model.c`** - Default/fallback model implementation

##### `src/llm/providers/` - API Provider Implementations
- **`anthropic_provider.c`** - Anthropic API client
- **`openai_provider.c`** - OpenAI API client
- **`local_ai_provider.c`** - Local AI server integration (LM Studio, Ollama)

#### `src/network/` - Network Communication
- **`http_client.c/h`** - HTTP client implementation for API calls
- **`api_common.c/h`** - Common API utilities and request/response handling

#### `src/session/` - User Session Management
- **`session_manager.c/h`** - Overall session lifecycle management
- **`conversation_tracker.c/h`** - Conversation history tracking and persistence
- **`conversation_compactor.c/h`** - Intelligent conversation history compression
- **`token_manager.c/h`** - Token counting and context window management

#### `src/tools/` - AI Tool System
- **`tools_system.c/h`** - Core tool execution framework
- **`tools_system_safe.c`** - Safe tool execution environment
- **`tool_result_builder.c/h`** - Tool result formatting and presentation

##### Individual Tools
- **`file_tools.c/h`** - File system operations (read, write, list)
- **`shell_tool.c/h`** - Shell command execution
- **`memory_tool.c/h`** - Persistent memory management
- **`todo_manager.c/h`** & **`todo_tool.c/h`** & **`todo_display.c/h`** - Task management system
- **`pdf_tool.c/h`** - PDF processing and extraction
- **`vector_db_tool.c/h`** - Vector database operations
- **`links_tool.c/h`** - URL and link processing

#### `src/db/` - Database Layer
- **`vector_db.c/h`** - Core vector database functionality
- **`vector_db_service.c/h`** - Vector database service layer
- **`hnswlib_wrapper.cpp/h`** - C++ wrapper for HNSW vector indexing

#### `src/pdf/` - PDF Processing
- **`pdf_extractor.c/h`** - PDF text and metadata extraction

#### `src/utils/` - Utility Functions
- **`common_utils.c/h`** - General utility functions
- **`env_loader.c/h`** - Environment variable and configuration loading
- **`json_utils.c/h`** - JSON parsing and manipulation
- **`output_formatter.c/h`** - Response formatting and display
- **`debug_output.c/h`** - Debug logging and output
- **`prompt_loader.c/h`** - System prompt loading and management
- **`context_retriever.c/h`** - Context retrieval for conversations
- **`document_chunker.c/h`** - Text chunking for vector storage
- **`pdf_processor.c/h`** - PDF processing utilities

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

#### `test/utils/` - Utility Tests
- **`test_env_loader.c`** - Configuration loading tests
- **`test_output_formatter.c`** - Output formatting tests
- **`test_prompt_loader.c`** - Prompt loading tests

#### Test Infrastructure
- **`test/unity/`** - Unity testing framework (vendored)
- **`mock_api_server.c/h`** - Mock API server for testing

## Key Architectural Components

### 1. **Multi-Provider LLM System**
The `src/llm/` directory contains a flexible provider system supporting multiple AI services (OpenAI, Anthropic, local servers) with model-specific optimizations.

### 2. **Comprehensive Tool System**
The `src/tools/` directory implements a safe, extensible tool execution framework allowing the AI to interact with files, shell, memory, and more.

### 3. **Vector Database Integration**
The `src/db/` directory provides persistent memory through vector embeddings, enabling long-term context retention.

### 4. **Session Management**
The `src/session/` directory handles conversation persistence, intelligent compression, and token management across different model context windows.

### 5. **Network Abstraction**
The `src/network/` directory provides a robust HTTP client with provider-specific API handling.

### 6. **PDF Processing Pipeline**
The `src/pdf/` directory enables document ingestion and knowledge extraction from PDF files.

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

This architecture enables Ralph to be a portable, powerful AI development companion with persistent memory, multi-provider support, and comprehensive tool integration.