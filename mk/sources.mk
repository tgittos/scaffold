# Source file definitions for ralph

# Core application sources
CORE_SOURCES := \
    $(SRCDIR)/core/main.c \
    $(SRCDIR)/core/ralph.c \
    $(SRCDIR)/core/tool_executor.c \
    $(SRCDIR)/core/streaming_handler.c \
    $(SRCDIR)/core/context_enhancement.c \
    $(SRCDIR)/core/recap.c \
    $(SRCDIR)/core/approval_gate.c \
    $(SRCDIR)/core/atomic_file.c \
    $(SRCDIR)/core/path_normalize.c \
    $(SRCDIR)/core/protected_files.c \
    $(SRCDIR)/core/shell_parser.c \
    $(SRCDIR)/core/shell_parser_cmd.c \
    $(SRCDIR)/core/shell_parser_ps.c \
    $(SRCDIR)/core/subagent_approval.c \
    $(SRCDIR)/network/http_client.c \
    $(SRCDIR)/network/embedded_cacert.c \
    $(SRCDIR)/network/streaming.c \
    $(SRCDIR)/network/api_common.c \
    $(SRCDIR)/network/api_error.c \
    $(SRCDIR)/utils/config.c \
    $(SRCDIR)/utils/output_formatter.c \
    $(SRCDIR)/utils/json_output.c \
    $(SRCDIR)/utils/prompt_loader.c \
    $(SRCDIR)/utils/document_chunker.c \
    $(SRCDIR)/utils/pdf_processor.c \
    $(SRCDIR)/utils/context_retriever.c \
    $(SRCDIR)/utils/common_utils.c \
    $(SRCDIR)/utils/debug_output.c \
    $(SRCDIR)/utils/json_escape.c \
    $(SRCDIR)/session/conversation_tracker.c \
    $(SRCDIR)/session/conversation_compactor.c \
    $(SRCDIR)/session/session_manager.c \
    $(SRCDIR)/session/token_manager.c \
    $(SRCDIR)/llm/llm_provider.c

# Tool system
TOOL_SOURCES := \
    $(SRCDIR)/tools/tools_system.c \
    $(SRCDIR)/tools/todo_manager.c \
    $(SRCDIR)/tools/todo_tool.c \
    $(SRCDIR)/tools/todo_display.c \
    $(SRCDIR)/tools/vector_db_tool.c \
    $(SRCDIR)/tools/memory_tool.c \
    $(SRCDIR)/tools/pdf_tool.c \
    $(SRCDIR)/tools/tool_result_builder.c \
    $(SRCDIR)/tools/subagent_tool.c \
    $(SRCDIR)/tools/python_tool.c \
    $(SRCDIR)/tools/python_tool_files.c

# MCP system
MCP_SOURCES := $(SRCDIR)/mcp/mcp_client.c

# LLM providers
PROVIDER_SOURCES := \
    $(SRCDIR)/llm/providers/openai_provider.c \
    $(SRCDIR)/llm/providers/anthropic_provider.c \
    $(SRCDIR)/llm/providers/local_ai_provider.c \
    $(SRCDIR)/llm/embeddings.c \
    $(SRCDIR)/llm/embeddings_service.c \
    $(SRCDIR)/llm/embedding_provider.c \
    $(SRCDIR)/llm/providers/openai_embedding_provider.c \
    $(SRCDIR)/llm/providers/local_embedding_provider.c

# Model implementations
MODEL_SOURCES := \
    $(SRCDIR)/llm/model_capabilities.c \
    $(SRCDIR)/llm/models/response_processing.c \
    $(SRCDIR)/llm/models/qwen_model.c \
    $(SRCDIR)/llm/models/deepseek_model.c \
    $(SRCDIR)/llm/models/gpt_model.c \
    $(SRCDIR)/llm/models/claude_model.c \
    $(SRCDIR)/llm/models/default_model.c

# Database
DB_C_SOURCES := \
    $(SRCDIR)/db/vector_db.c \
    $(SRCDIR)/db/vector_db_service.c \
    $(SRCDIR)/db/metadata_store.c \
    $(SRCDIR)/db/document_store.c \
    $(SRCDIR)/db/task_store.c

DB_CPP_SOURCES := $(SRCDIR)/db/hnswlib_wrapper.cpp

# PDF
PDF_SOURCES := $(SRCDIR)/pdf/pdf_extractor.c

# Utilities
UTILS_EXTRA_SOURCES := \
    $(SRCDIR)/utils/uuid_utils.c \
    $(SRCDIR)/utils/ralph_home.c

# CLI
CLI_SOURCES := $(SRCDIR)/cli/memory_commands.c

# Combined sources
C_SOURCES := $(CORE_SOURCES) $(TOOL_SOURCES) $(MCP_SOURCES) $(PROVIDER_SOURCES) \
    $(MODEL_SOURCES) $(DB_C_SOURCES) $(PDF_SOURCES) $(CLI_SOURCES) $(UTILS_EXTRA_SOURCES)
CPP_SOURCES := $(DB_CPP_SOURCES)
SOURCES := $(C_SOURCES) $(CPP_SOURCES)
OBJECTS := $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)
HEADERS := $(wildcard $(SRCDIR)/*/*.h) $(SRCDIR)/embedded_links.h

# Reusable dependency groups for tests
NETWORK_DEPS := \
    $(SRCDIR)/network/http_client.c \
    $(SRCDIR)/network/embedded_cacert.c \
    $(SRCDIR)/network/api_error.c

EMBEDDING_DEPS := \
    $(SRCDIR)/llm/embeddings.c \
    $(SRCDIR)/llm/embeddings_service.c \
    $(SRCDIR)/llm/embedding_provider.c \
    $(SRCDIR)/llm/providers/openai_embedding_provider.c \
    $(SRCDIR)/llm/providers/local_embedding_provider.c

UTIL_DEPS := \
    $(SRCDIR)/utils/json_escape.c \
    $(SRCDIR)/utils/output_formatter.c \
    $(SRCDIR)/utils/json_output.c \
    $(SRCDIR)/utils/debug_output.c \
    $(SRCDIR)/utils/common_utils.c \
    $(SRCDIR)/utils/document_chunker.c \
    $(SRCDIR)/utils/pdf_processor.c \
    $(SRCDIR)/utils/context_retriever.c \
    $(SRCDIR)/utils/config.c \
    $(SRCDIR)/utils/uuid_utils.c \
    $(SRCDIR)/utils/ralph_home.c

RALPH_CORE_DEPS := \
    $(SRCDIR)/core/ralph.c \
    $(SRCDIR)/core/tool_executor.c \
    $(SRCDIR)/core/streaming_handler.c \
    $(SRCDIR)/core/context_enhancement.c \
    $(SRCDIR)/core/recap.c \
    $(SRCDIR)/utils/env_loader.c \
    $(SRCDIR)/utils/prompt_loader.c \
    $(SRCDIR)/session/conversation_tracker.c \
    $(SRCDIR)/session/conversation_compactor.c \
    $(SRCDIR)/session/session_manager.c \
    $(SRCDIR)/network/api_common.c \
    $(SRCDIR)/network/streaming.c \
    $(SRCDIR)/session/token_manager.c \
    $(SRCDIR)/llm/llm_provider.c \
    $(SRCDIR)/llm/providers/openai_provider.c \
    $(SRCDIR)/llm/providers/anthropic_provider.c \
    $(SRCDIR)/llm/providers/local_ai_provider.c \
    $(SRCDIR)/mcp/mcp_client.c

COMPLEX_DEPS := \
    $(TOOL_SOURCES) \
    $(MODEL_SOURCES) \
    $(UTIL_DEPS) \
    $(DB_C_SOURCES) \
    $(EMBEDDING_DEPS) \
    $(SRCDIR)/pdf/pdf_extractor.c \
    $(NETWORK_DEPS)

CONV_DEPS := \
    $(SRCDIR)/session/conversation_tracker.c \
    $(DB_C_SOURCES) \
    $(EMBEDDING_DEPS) \
    $(NETWORK_DEPS) \
    $(SRCDIR)/utils/json_escape.c \
    $(SRCDIR)/utils/env_loader.c \
    $(SRCDIR)/utils/config.c \
    $(SRCDIR)/utils/debug_output.c \
    $(SRCDIR)/utils/common_utils.c \
    $(SRCDIR)/utils/ralph_home.c
