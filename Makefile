# ralph Makefile - Optimized for AI assistance
# Built with Cosmopolitan for universal binary compatibility
# Structured for easy navigation and modification

# =============================================================================
# CONFIGURATION
# =============================================================================

CC := cosmocc
CXX := cosmoc++
CFLAGS := -Wall -Wextra -Werror -O2 -std=c11 -DHAVE_PDFIO
CXXFLAGS := -Wall -Wextra -Werror -O2 -std=c++14 -Wno-unused-parameter -Wno-unused-function -Wno-type-limits
TARGET := ralph

# Directory structure
SRCDIR := src
TESTDIR := test
DEPDIR := deps
BUILDDIR := build

# Dependency versions
CURL_VERSION := 8.4.0
MBEDTLS_VERSION := 3.5.1
HNSWLIB_VERSION := 0.8.0
PDFIO_VERSION := 1.3.1
ZLIB_VERSION := 1.3.1
CJSON_VERSION := 1.7.18
READLINE_VERSION := 8.2
NCURSES_VERSION := 6.4

# =============================================================================
# SOURCE FILES
# =============================================================================

# Core application sources
CORE_SOURCES := $(SRCDIR)/core/main.c \
                $(SRCDIR)/core/ralph.c \
                $(SRCDIR)/network/http_client.c \
                $(SRCDIR)/network/api_common.c \
                $(SRCDIR)/utils/config.c \
                $(SRCDIR)/utils/output_formatter.c \
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
TOOL_SOURCES := $(SRCDIR)/tools/tools_system.c \
                $(SRCDIR)/tools/shell_tool.c \
                $(SRCDIR)/tools/file_tools.c \
                $(SRCDIR)/tools/links_tool.c \
                $(SRCDIR)/tools/todo_manager.c \
                $(SRCDIR)/tools/todo_tool.c \
                $(SRCDIR)/tools/todo_display.c \
                $(SRCDIR)/tools/vector_db_tool.c \
                $(SRCDIR)/tools/memory_tool.c \
                $(SRCDIR)/tools/pdf_tool.c \
                $(SRCDIR)/tools/tool_result_builder.c

# MCP system
MCP_SOURCES := $(SRCDIR)/mcp/mcp_client.c

# LLM providers and models
PROVIDER_SOURCES := $(SRCDIR)/llm/providers/openai_provider.c \
                    $(SRCDIR)/llm/providers/anthropic_provider.c \
                    $(SRCDIR)/llm/providers/local_ai_provider.c \
                    $(SRCDIR)/llm/embeddings.c \
                    $(SRCDIR)/llm/embeddings_service.c \
                    $(SRCDIR)/llm/embedding_provider.c \
                    $(SRCDIR)/llm/providers/openai_embedding_provider.c \
                    $(SRCDIR)/llm/providers/local_embedding_provider.c

MODEL_SOURCES := $(SRCDIR)/llm/model_capabilities.c \
                 $(SRCDIR)/llm/models/qwen_model.c \
                 $(SRCDIR)/llm/models/deepseek_model.c \
                 $(SRCDIR)/llm/models/gpt_model.c \
                 $(SRCDIR)/llm/models/claude_model.c \
                 $(SRCDIR)/llm/models/default_model.c

# Database and PDF
DB_C_SOURCES := $(SRCDIR)/db/vector_db.c $(SRCDIR)/db/vector_db_service.c $(SRCDIR)/db/metadata_store.c $(SRCDIR)/db/document_store.c
DB_CPP_SOURCES := $(SRCDIR)/db/hnswlib_wrapper.cpp
PDF_SOURCES := $(SRCDIR)/pdf/pdf_extractor.c

# CLI commands
CLI_SOURCES := $(SRCDIR)/cli/memory_commands.c

# Combined sources
C_SOURCES := $(CORE_SOURCES) $(TOOL_SOURCES) $(MCP_SOURCES) $(PROVIDER_SOURCES) $(MODEL_SOURCES) $(DB_C_SOURCES) $(PDF_SOURCES) $(CLI_SOURCES)
CPP_SOURCES := $(DB_CPP_SOURCES)
SOURCES := $(C_SOURCES) $(CPP_SOURCES)
OBJECTS := $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)
HEADERS := $(wildcard $(SRCDIR)/*/*.h) $(SRCDIR)/embedded_links.h

# =============================================================================
# DEPENDENCIES & LIBRARIES
# =============================================================================

# Dependency paths
CURL_DIR = $(DEPDIR)/curl-$(CURL_VERSION)
MBEDTLS_DIR = $(DEPDIR)/mbedtls-$(MBEDTLS_VERSION)
HNSWLIB_DIR = $(DEPDIR)/hnswlib-$(HNSWLIB_VERSION)
PDFIO_DIR = $(DEPDIR)/pdfio-$(PDFIO_VERSION)
ZLIB_DIR = $(DEPDIR)/zlib-$(ZLIB_VERSION)
CJSON_DIR = $(DEPDIR)/cJSON-$(CJSON_VERSION)
READLINE_DIR = $(DEPDIR)/readline-$(READLINE_VERSION)
NCURSES_DIR = $(DEPDIR)/ncurses-$(NCURSES_VERSION)

# Library files
CURL_LIB = $(CURL_DIR)/lib/.libs/libcurl.a
MBEDTLS_LIB1 = $(MBEDTLS_DIR)/library/libmbedtls.a
MBEDTLS_LIB2 = $(MBEDTLS_DIR)/library/libmbedx509.a  
MBEDTLS_LIB3 = $(MBEDTLS_DIR)/library/libmbedcrypto.a
PDFIO_LIB = $(PDFIO_DIR)/libpdfio.a
ZLIB_LIB = $(ZLIB_DIR)/libz.a
CJSON_LIB = $(CJSON_DIR)/libcjson.a
READLINE_LIB = $(READLINE_DIR)/libreadline.a
HISTORY_LIB = $(READLINE_DIR)/libhistory.a
NCURSES_LIB = $(NCURSES_DIR)/lib/libncurses.a

# All required libraries
ALL_LIBS := $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(READLINE_LIB) $(HISTORY_LIB) $(NCURSES_LIB)

# Include and library flags
INCLUDES = -I$(CURL_DIR)/include -I$(MBEDTLS_DIR)/include -I$(HNSWLIB_DIR) -I$(PDFIO_DIR) -I$(ZLIB_DIR) -I$(CJSON_DIR) -I$(READLINE_DIR) -I$(READLINE_DIR)/readline -I$(NCURSES_DIR)/include -I$(SRCDIR) -I$(SRCDIR)/core -I$(SRCDIR)/network -I$(SRCDIR)/llm -I$(SRCDIR)/session -I$(SRCDIR)/tools -I$(SRCDIR)/utils -I$(SRCDIR)/db -I$(SRCDIR)/pdf -I$(SRCDIR)/cli
TEST_INCLUDES = $(INCLUDES) -I$(TESTDIR)/unity -I$(TESTDIR) -I$(TESTDIR)/core -I$(TESTDIR)/network -I$(TESTDIR)/llm -I$(TESTDIR)/session -I$(TESTDIR)/tools -I$(TESTDIR)/utils
LDFLAGS = -L$(CURL_DIR)/lib/.libs -L$(MBEDTLS_DIR)/library -L$(PDFIO_DIR) -L$(ZLIB_DIR) -L$(CJSON_DIR) -L$(READLINE_DIR) -L$(NCURSES_DIR)/lib
LIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(READLINE_LIB) $(HISTORY_LIB) $(NCURSES_LIB) -lm

# =============================================================================
# TOOLS AND UTILITIES
# =============================================================================

BIN2C := $(BUILDDIR)/bin2c
LINKS_BUNDLED := $(BUILDDIR)/links
EMBEDDED_LINKS_HEADER := $(SRCDIR)/embedded_links.h

# =============================================================================
# COMMON TEST COMPONENTS
# =============================================================================

# Common test dependencies - most tests need these core components
COMMON_TEST_SOURCES := $(TESTDIR)/unity/unity.c
TOOL_TEST_DEPS := $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(SRCDIR)/tools/memory_tool.c $(SRCDIR)/tools/pdf_tool.c $(SRCDIR)/tools/tool_result_builder.c
MODEL_TEST_DEPS := $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c
UTIL_TEST_DEPS := $(SRCDIR)/utils/json_escape.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/utils/common_utils.c $(SRCDIR)/utils/document_chunker.c $(SRCDIR)/utils/pdf_processor.c $(SRCDIR)/utils/context_retriever.c $(SRCDIR)/utils/config.c
COMPLEX_TEST_DEPS := $(TOOL_TEST_DEPS) $(MODEL_TEST_DEPS) $(UTIL_TEST_DEPS) $(DB_C_SOURCES) $(SRCDIR)/pdf/pdf_extractor.c $(SRCDIR)/network/http_client.c

# =============================================================================
# TEST DEFINITIONS - Simple and explicit for easy AI navigation
# =============================================================================

# Simple tests (minimal dependencies)
TEST_MAIN_SOURCES = $(TESTDIR)/core/test_main.c $(COMMON_TEST_SOURCES)
TEST_MAIN_OBJECTS = $(TEST_MAIN_SOURCES:.c=.o)
TEST_MAIN_TARGET = $(TESTDIR)/test_main

TEST_ENV_SOURCES = $(TESTDIR)/utils/test_env_loader.c $(SRCDIR)/utils/env_loader.c $(COMMON_TEST_SOURCES)
TEST_ENV_OBJECTS = $(TEST_ENV_SOURCES:.c=.o)
TEST_ENV_TARGET = $(TESTDIR)/test_env_loader

TEST_CONFIG_SOURCES = $(TESTDIR)/utils/test_config.c $(SRCDIR)/utils/config.c $(COMMON_TEST_SOURCES)
TEST_CONFIG_OBJECTS = $(TEST_CONFIG_SOURCES:.c=.o)
TEST_CONFIG_TARGET = $(TESTDIR)/test_config

TEST_PROMPT_SOURCES = $(TESTDIR)/utils/test_prompt_loader.c $(SRCDIR)/utils/prompt_loader.c $(COMMON_TEST_SOURCES)
TEST_PROMPT_OBJECTS = $(TEST_PROMPT_SOURCES:.c=.o)
TEST_PROMPT_TARGET = $(TESTDIR)/test_prompt_loader

TEST_CONVERSATION_SOURCES = $(TESTDIR)/session/test_conversation_tracker.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/utils/json_escape.c $(COMMON_TEST_SOURCES)
TEST_CONVERSATION_OBJECTS = $(TEST_CONVERSATION_SOURCES:.c=.o)
TEST_CONVERSATION_TARGET = $(TESTDIR)/test_conversation_tracker

TEST_CONVERSATION_VDB_C_SOURCES = $(TESTDIR)/session/test_conversation_vector_db.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/db/document_store.c $(SRCDIR)/db/vector_db.c $(SRCDIR)/db/metadata_store.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c $(SRCDIR)/network/http_client.c $(SRCDIR)/utils/json_escape.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/config.c $(SRCDIR)/utils/debug_output.c $(COMMON_TEST_SOURCES)
TEST_CONVERSATION_VDB_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_CONVERSATION_VDB_OBJECTS = $(TEST_CONVERSATION_VDB_C_SOURCES:.c=.o) $(TEST_CONVERSATION_VDB_CPP_SOURCES:.cpp=.o)
TEST_CONVERSATION_VDB_TARGET = $(TESTDIR)/test_conversation_vector_db

TEST_TODO_MANAGER_SOURCES = $(TESTDIR)/tools/test_todo_manager.c $(SRCDIR)/tools/todo_manager.c $(COMMON_TEST_SOURCES)
TEST_TODO_MANAGER_OBJECTS = $(TEST_TODO_MANAGER_SOURCES:.c=.o)
TEST_TODO_MANAGER_TARGET = $(TESTDIR)/test_todo_manager

TEST_TODO_TOOL_C_SOURCES = $(TESTDIR)/tools/test_todo_tool.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_TODO_TOOL_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_TODO_TOOL_OBJECTS = $(TEST_TODO_TOOL_C_SOURCES:.c=.o) $(TEST_TODO_TOOL_CPP_SOURCES:.cpp=.o)
TEST_TODO_TOOL_TARGET = $(TESTDIR)/test_todo_tool

TEST_PDF_EXTRACTOR_SOURCES = $(TESTDIR)/pdf/test_pdf_extractor.c $(SRCDIR)/pdf/pdf_extractor.c $(COMMON_TEST_SOURCES)
TEST_PDF_EXTRACTOR_OBJECTS = $(TEST_PDF_EXTRACTOR_SOURCES:.c=.o)
TEST_PDF_EXTRACTOR_TARGET = $(TESTDIR)/test_pdf_extractor

TEST_DOCUMENT_CHUNKER_SOURCES = $(TESTDIR)/test_document_chunker.c $(SRCDIR)/utils/document_chunker.c $(COMMON_TEST_SOURCES)
TEST_DOCUMENT_CHUNKER_OBJECTS = $(TEST_DOCUMENT_CHUNKER_SOURCES:.c=.o)
TEST_DOCUMENT_CHUNKER_TARGET = $(TESTDIR)/test_document_chunker

# Complex tests (with full dependencies)
TEST_HTTP_C_SOURCES = $(TESTDIR)/network/test_http_client.c $(SRCDIR)/utils/env_loader.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_HTTP_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_HTTP_OBJECTS = $(TEST_HTTP_C_SOURCES:.c=.o) $(TEST_HTTP_CPP_SOURCES:.cpp=.o)
TEST_HTTP_TARGET = $(TESTDIR)/test_http_client

TEST_OUTPUT_C_SOURCES = $(TESTDIR)/utils/test_output_formatter.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_OUTPUT_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_OUTPUT_OBJECTS = $(TEST_OUTPUT_C_SOURCES:.c=.o) $(TEST_OUTPUT_CPP_SOURCES:.cpp=.o)
TEST_OUTPUT_TARGET = $(TESTDIR)/test_output_formatter

TEST_TOOLS_C_SOURCES = $(TESTDIR)/tools/test_tools_system.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_TOOLS_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_TOOLS_OBJECTS = $(TEST_TOOLS_C_SOURCES:.c=.o) $(TEST_TOOLS_CPP_SOURCES:.cpp=.o)
TEST_TOOLS_TARGET = $(TESTDIR)/test_tools_system

TEST_SHELL_C_SOURCES = $(TESTDIR)/tools/test_shell_tool.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_SHELL_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_SHELL_OBJECTS = $(TEST_SHELL_C_SOURCES:.c=.o) $(TEST_SHELL_CPP_SOURCES:.cpp=.o)
TEST_SHELL_TARGET = $(TESTDIR)/test_shell_tool

TEST_FILE_C_SOURCES = $(TESTDIR)/tools/test_file_tools.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_FILE_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_FILE_OBJECTS = $(TEST_FILE_C_SOURCES:.c=.o) $(TEST_FILE_CPP_SOURCES:.cpp=.o)
TEST_FILE_TARGET = $(TESTDIR)/test_file_tools

TEST_SMART_FILE_C_SOURCES = $(TESTDIR)/tools/test_smart_file_tools.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_SMART_FILE_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_SMART_FILE_OBJECTS = $(TEST_SMART_FILE_C_SOURCES:.c=.o) $(TEST_SMART_FILE_CPP_SOURCES:.cpp=.o)
TEST_SMART_FILE_TARGET = $(TESTDIR)/test_smart_file_tools

TEST_VECTOR_DB_TOOL_C_SOURCES = $(TESTDIR)/tools/test_vector_db_tool.c $(SRCDIR)/utils/env_loader.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_VECTOR_DB_TOOL_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_VECTOR_DB_TOOL_OBJECTS = $(TEST_VECTOR_DB_TOOL_C_SOURCES:.c=.o) $(TEST_VECTOR_DB_TOOL_CPP_SOURCES:.cpp=.o)
TEST_VECTOR_DB_TOOL_TARGET = $(TESTDIR)/test_vector_db_tool

TEST_MEMORY_TOOL_C_SOURCES = $(TESTDIR)/tools/test_memory_tool.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_MEMORY_TOOL_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MEMORY_TOOL_OBJECTS = $(TEST_MEMORY_TOOL_C_SOURCES:.c=.o) $(TEST_MEMORY_TOOL_CPP_SOURCES:.cpp=.o)
TEST_MEMORY_TOOL_TARGET = $(TESTDIR)/test_memory_tool

TEST_MEMORY_HYBRID_C_SOURCES = $(TESTDIR)/test_memory_hybrid_search.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_MEMORY_HYBRID_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MEMORY_HYBRID_OBJECTS = $(TEST_MEMORY_HYBRID_C_SOURCES:.c=.o) $(TEST_MEMORY_HYBRID_CPP_SOURCES:.cpp=.o)
TEST_MEMORY_HYBRID_TARGET = $(TESTDIR)/test_memory_hybrid_search

TEST_MEMORY_MGMT_C_SOURCES = $(TESTDIR)/test_memory_management.c $(SRCDIR)/cli/memory_commands.c $(DB_C_SOURCES) $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c $(SRCDIR)/utils/config.c $(SRCDIR)/network/http_client.c $(SRCDIR)/utils/common_utils.c $(SRCDIR)/utils/debug_output.c $(COMMON_TEST_SOURCES)
TEST_MEMORY_MGMT_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MEMORY_MGMT_OBJECTS = $(TEST_MEMORY_MGMT_C_SOURCES:.c=.o) $(TEST_MEMORY_MGMT_CPP_SOURCES:.cpp=.o)
TEST_MEMORY_MGMT_TARGET = $(TESTDIR)/test_memory_management

TEST_TOKEN_MANAGER_C_SOURCES = $(TESTDIR)/session/test_token_manager.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/session/conversation_tracker.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_TOKEN_MANAGER_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_TOKEN_MANAGER_OBJECTS = $(TEST_TOKEN_MANAGER_C_SOURCES:.c=.o) $(TEST_TOKEN_MANAGER_CPP_SOURCES:.cpp=.o)
TEST_TOKEN_MANAGER_TARGET = $(TESTDIR)/test_token_manager

TEST_CONVERSATION_COMPACTOR_C_SOURCES = $(TESTDIR)/session/test_conversation_compactor.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/token_manager.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_CONVERSATION_COMPACTOR_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_CONVERSATION_COMPACTOR_OBJECTS = $(TEST_CONVERSATION_COMPACTOR_C_SOURCES:.c=.o) $(TEST_CONVERSATION_COMPACTOR_CPP_SOURCES:.cpp=.o)
TEST_CONVERSATION_COMPACTOR_TARGET = $(TESTDIR)/test_conversation_compactor

TEST_RALPH_C_SOURCES = $(TESTDIR)/core/test_ralph.c $(TESTDIR)/mock_api_server.c $(SRCDIR)/core/ralph.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/mcp/mcp_client.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_RALPH_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_RALPH_OBJECTS = $(TEST_RALPH_C_SOURCES:.c=.o) $(TEST_RALPH_CPP_SOURCES:.cpp=.o)
TEST_RALPH_TARGET = $(TESTDIR)/test_ralph

TEST_INCOMPLETE_TASK_BUG_C_SOURCES = $(TESTDIR)/core/test_incomplete_task_bug.c $(SRCDIR)/core/ralph.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/mcp/mcp_client.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_INCOMPLETE_TASK_BUG_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_INCOMPLETE_TASK_BUG_OBJECTS = $(TEST_INCOMPLETE_TASK_BUG_C_SOURCES:.c=.o) $(TEST_INCOMPLETE_TASK_BUG_CPP_SOURCES:.cpp=.o)
TEST_INCOMPLETE_TASK_BUG_TARGET = $(TESTDIR)/test_incomplete_task_bug

TEST_MODEL_TOOLS_C_SOURCES = $(TESTDIR)/llm/test_model_tools.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_MODEL_TOOLS_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MODEL_TOOLS_OBJECTS = $(TEST_MODEL_TOOLS_C_SOURCES:.c=.o) $(TEST_MODEL_TOOLS_CPP_SOURCES:.cpp=.o)
TEST_MODEL_TOOLS_TARGET = $(TESTDIR)/test_model_tools

TEST_MESSAGES_ARRAY_BUG_C_SOURCES = $(TESTDIR)/network/test_messages_array_bug.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/conversation_tracker.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_MESSAGES_ARRAY_BUG_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MESSAGES_ARRAY_BUG_OBJECTS = $(TEST_MESSAGES_ARRAY_BUG_C_SOURCES:.c=.o) $(TEST_MESSAGES_ARRAY_BUG_CPP_SOURCES:.cpp=.o)
TEST_MESSAGES_ARRAY_BUG_TARGET = $(TESTDIR)/test_messages_array_bug

# MCP test
TEST_MCP_CLIENT_C_SOURCES = $(TESTDIR)/mcp/test_mcp_client.c $(SRCDIR)/mcp/mcp_client.c $(SRCDIR)/core/ralph.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(COMPLEX_TEST_DEPS) $(COMMON_TEST_SOURCES)
TEST_MCP_CLIENT_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MCP_CLIENT_OBJECTS = $(TEST_MCP_CLIENT_C_SOURCES:.c=.o) $(TEST_MCP_CLIENT_CPP_SOURCES:.cpp=.o)
TEST_MCP_CLIENT_TARGET = $(TESTDIR)/test_mcp_client

# Special vector DB test
TEST_VECTOR_DB_SOURCES = $(TESTDIR)/db/test_vector_db.c $(SRCDIR)/db/vector_db.c $(SRCDIR)/db/hnswlib_wrapper.cpp $(TESTDIR)/unity/unity.c
TEST_VECTOR_DB_C_OBJECTS = $(TESTDIR)/db/test_vector_db.o $(SRCDIR)/db/vector_db.o $(TESTDIR)/unity/unity.o
TEST_VECTOR_DB_CPP_OBJECTS = $(SRCDIR)/db/hnswlib_wrapper.o
TEST_VECTOR_DB_OBJECTS = $(TEST_VECTOR_DB_C_OBJECTS) $(TEST_VECTOR_DB_CPP_OBJECTS)
TEST_VECTOR_DB_TARGET = $(TESTDIR)/test_vector_db

# Document store test
TEST_DOCUMENT_STORE_C_SOURCES = $(TESTDIR)/db/test_document_store.c $(DB_C_SOURCES) $(LLM_SOURCES) $(NETWORK_SOURCES) $(UTILS_SOURCES) $(SESSION_SOURCES) $(TESTDIR)/unity/unity.c
TEST_DOCUMENT_STORE_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_DOCUMENT_STORE_C_OBJECTS = $(TEST_DOCUMENT_STORE_C_SOURCES:.c=.o)
TEST_DOCUMENT_STORE_CPP_OBJECTS = $(TEST_DOCUMENT_STORE_CPP_SOURCES:.cpp=.o)
TEST_DOCUMENT_STORE_OBJECTS = $(TEST_DOCUMENT_STORE_C_OBJECTS) $(TEST_DOCUMENT_STORE_CPP_OBJECTS)
TEST_DOCUMENT_STORE_TARGET = $(TESTDIR)/test_document_store

# Collect all test targets
ALL_TEST_TARGETS = $(TEST_MAIN_TARGET) $(TEST_ENV_TARGET) $(TEST_CONFIG_TARGET) $(TEST_PROMPT_TARGET) $(TEST_CONVERSATION_TARGET) $(TEST_CONVERSATION_VDB_TARGET) $(TEST_TODO_MANAGER_TARGET) $(TEST_TODO_TOOL_TARGET) $(TEST_PDF_EXTRACTOR_TARGET) $(TEST_DOCUMENT_CHUNKER_TARGET) $(TEST_HTTP_TARGET) $(TEST_OUTPUT_TARGET) $(TEST_TOOLS_TARGET) $(TEST_SHELL_TARGET) $(TEST_FILE_TARGET) $(TEST_SMART_FILE_TARGET) $(TEST_VECTOR_DB_TOOL_TARGET) $(TEST_MEMORY_TOOL_TARGET) $(TEST_MEMORY_HYBRID_TARGET) $(TEST_MEMORY_MGMT_TARGET) $(TEST_TOKEN_MANAGER_TARGET) $(TEST_CONVERSATION_COMPACTOR_TARGET) $(TEST_RALPH_TARGET) $(TEST_INCOMPLETE_TASK_BUG_TARGET) $(TEST_MODEL_TOOLS_TARGET) $(TEST_MESSAGES_ARRAY_BUG_TARGET) $(TEST_VECTOR_DB_TARGET) $(TEST_DOCUMENT_STORE_TARGET) $(TEST_MCP_CLIENT_TARGET)

# =============================================================================
# BUILD RULES
# =============================================================================

# Default target
all: $(TARGET)

# Main executable
$(TARGET): $(EMBEDDED_LINKS_HEADER) $(OBJECTS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	@echo "Linking with PDFio support"
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS) -lpthread

# Embedded links
$(BIN2C): build/bin2c.c
	$(CC) -O2 -o $@ $<

$(EMBEDDED_LINKS_HEADER): $(LINKS_BUNDLED) $(BIN2C)
	$(BIN2C) $(LINKS_BUNDLED) embedded_links > $(EMBEDDED_LINKS_HEADER)

$(LINKS_BUNDLED):
	@echo "Checking for pre-built Cosmopolitan Links binary..."
	@if [ ! -f $(LINKS_BUNDLED) ]; then \
		echo "Downloading pre-built Cosmopolitan Links binary..."; \
		curl -L -o $(LINKS_BUNDLED) https://cosmo.zip/pub/cosmos/bin/links || \
		wget -O $(LINKS_BUNDLED) https://cosmo.zip/pub/cosmos/bin/links; \
		chmod +x $(LINKS_BUNDLED); \
	else \
		echo "Using existing $(LINKS_BUNDLED)"; \
	fi

# Compilation rules
$(SRCDIR)/tools/links_tool.o: $(SRCDIR)/tools/links_tool.c $(EMBEDDED_LINKS_HEADER) $(HEADERS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# =============================================================================
# TEST BUILD RULES - Simple and explicit
# =============================================================================

# Simple test linking (no external libraries)
$(TEST_MAIN_TARGET): $(TEST_MAIN_OBJECTS)
	$(CC) -o $@ $(TEST_MAIN_OBJECTS)

$(TEST_ENV_TARGET): $(TEST_ENV_OBJECTS)
	$(CC) -o $@ $(TEST_ENV_OBJECTS)

$(TEST_CONFIG_TARGET): $(TEST_CONFIG_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_CONFIG_OBJECTS) $(CJSON_LIB)

$(TEST_PROMPT_TARGET): $(TEST_PROMPT_OBJECTS)
	$(CC) -o $@ $(TEST_PROMPT_OBJECTS)

$(TEST_CONVERSATION_TARGET): $(TEST_CONVERSATION_OBJECTS) $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o src/network/http_client.o src/utils/env_loader.o src/utils/config.o src/utils/debug_output.o src/utils/common_utils.o $(ALL_LIBS)
	$(CXX) -o $@ test/session/test_conversation_tracker.o src/session/conversation_tracker.o $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o src/network/http_client.o src/utils/json_escape.o src/utils/env_loader.o src/utils/config.o src/utils/debug_output.o src/utils/common_utils.o test/unity/unity.o $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_CONVERSATION_VDB_TARGET): $(TEST_CONVERSATION_VDB_OBJECTS) $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o src/network/http_client.o src/utils/env_loader.o src/utils/config.o src/utils/debug_output.o src/utils/common_utils.o $(ALL_LIBS)
	$(CXX) -o $@ test/session/test_conversation_vector_db.o src/session/conversation_tracker.o $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o src/network/http_client.o src/utils/json_escape.o src/utils/env_loader.o src/utils/config.o src/utils/debug_output.o src/utils/common_utils.o test/unity/unity.o $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_TODO_MANAGER_TARGET): $(TEST_TODO_MANAGER_OBJECTS)
	$(CC) -o $@ $(TEST_TODO_MANAGER_OBJECTS)

$(TEST_TODO_TOOL_TARGET): $(TEST_TODO_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_TODO_TOOL_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_PDF_EXTRACTOR_TARGET): $(TEST_PDF_EXTRACTOR_OBJECTS) $(PDFIO_LIB)
	$(CC) -o $@ $(TEST_PDF_EXTRACTOR_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB) -lm

$(TEST_DOCUMENT_CHUNKER_TARGET): $(TEST_DOCUMENT_CHUNKER_OBJECTS)
	$(CC) -o $@ $(TEST_DOCUMENT_CHUNKER_OBJECTS)

# Complex test linking (with full libraries)
$(TEST_HTTP_TARGET): $(TEST_HTTP_OBJECTS) $(ALL_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_HTTP_OBJECTS) $(LIBS)

$(TEST_OUTPUT_TARGET): $(TEST_OUTPUT_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_OUTPUT_OBJECTS) src/session/conversation_tracker.o $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_TOOLS_TARGET): $(TEST_TOOLS_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_TOOLS_OBJECTS) src/session/conversation_tracker.o $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_SHELL_TARGET): $(TEST_SHELL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_SHELL_OBJECTS) src/session/conversation_tracker.o $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_FILE_TARGET): $(TEST_FILE_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_FILE_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_SMART_FILE_TARGET): $(TEST_SMART_FILE_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_SMART_FILE_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_VECTOR_DB_TOOL_TARGET): $(TEST_VECTOR_DB_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_VECTOR_DB_TOOL_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_MEMORY_TOOL_TARGET): $(TEST_MEMORY_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MEMORY_TOOL_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_MEMORY_HYBRID_TARGET): $(TEST_MEMORY_HYBRID_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MEMORY_HYBRID_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_MEMORY_MGMT_TARGET): $(TEST_MEMORY_MGMT_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MEMORY_MGMT_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_TOKEN_MANAGER_TARGET): $(TEST_TOKEN_MANAGER_OBJECTS) $(ALL_LIBS)
	$(CC) -o $@ $(TEST_TOKEN_MANAGER_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm

$(TEST_CONVERSATION_COMPACTOR_TARGET): $(TEST_CONVERSATION_COMPACTOR_OBJECTS) $(ALL_LIBS)
	$(CC) -o $@ $(TEST_CONVERSATION_COMPACTOR_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm

$(TEST_RALPH_TARGET): $(TEST_RALPH_OBJECTS) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_RALPH_OBJECTS) $(LIBS) -lpthread

$(TEST_INCOMPLETE_TASK_BUG_TARGET): $(TEST_INCOMPLETE_TASK_BUG_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_INCOMPLETE_TASK_BUG_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_MODEL_TOOLS_TARGET): $(TEST_MODEL_TOOLS_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MODEL_TOOLS_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_MESSAGES_ARRAY_BUG_TARGET): $(TEST_MESSAGES_ARRAY_BUG_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MESSAGES_ARRAY_BUG_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_MCP_CLIENT_TARGET): $(TEST_MCP_CLIENT_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MCP_CLIENT_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_VECTOR_DB_TARGET): $(TEST_VECTOR_DB_OBJECTS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) -o $@ $(TEST_VECTOR_DB_OBJECTS) -lpthread -lm

$(TEST_DOCUMENT_STORE_TARGET): $(TEST_DOCUMENT_STORE_OBJECTS) $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) src/utils/common_utils.o src/utils/config.o src/utils/debug_output.o src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o src/network/http_client.o $(ALL_LIBS)
	$(CXX) -o $@ test/db/test_document_store.o $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) src/utils/common_utils.o src/utils/config.o src/utils/debug_output.o src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o src/network/http_client.o test/unity/unity.o $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

# =============================================================================
# TEST EXECUTION
# =============================================================================

test: $(ALL_TEST_TARGETS)
	@echo "Running all tests..."
	./$(TEST_MAIN_TARGET)
	./$(TEST_HTTP_TARGET)
	./$(TEST_ENV_TARGET)
	./$(TEST_OUTPUT_TARGET)
	./$(TEST_PROMPT_TARGET)
	./$(TEST_CONVERSATION_TARGET)
	./$(TEST_CONVERSATION_VDB_TARGET)
	./$(TEST_TOOLS_TARGET)
	./$(TEST_SHELL_TARGET)
	./$(TEST_FILE_TARGET)
	./$(TEST_SMART_FILE_TARGET)
	./$(TEST_RALPH_TARGET)
	./$(TEST_TODO_MANAGER_TARGET)
	./$(TEST_TODO_TOOL_TARGET)
	./$(TEST_VECTOR_DB_TOOL_TARGET)
	./$(TEST_TOKEN_MANAGER_TARGET)
	./$(TEST_MODEL_TOOLS_TARGET)
	./$(TEST_CONVERSATION_COMPACTOR_TARGET)
	./$(TEST_INCOMPLETE_TASK_BUG_TARGET)
	./$(TEST_MESSAGES_ARRAY_BUG_TARGET)
	./$(TEST_MCP_CLIENT_TARGET)
	./$(TEST_VECTOR_DB_TARGET)
	./$(TEST_DOCUMENT_STORE_TARGET)
	./$(TEST_PDF_EXTRACTOR_TARGET)
	./$(TEST_DOCUMENT_CHUNKER_TARGET)
	@echo "All tests completed"

check: test

# Valgrind testing (safe subset)
check-valgrind: $(ALL_TEST_TARGETS)
	@echo "Running valgrind tests (excluding HTTP tests)..."
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MAIN_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_ENV_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_OUTPUT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PROMPT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_VDB_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TOOLS_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_SHELL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_FILE_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_RALPH_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TODO_MANAGER_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TODO_TOOL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_VECTOR_DB_TOOL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TOKEN_MANAGER_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_COMPACTOR_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MODEL_TOOLS_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_VECTOR_DB_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PDF_EXTRACTOR_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_DOCUMENT_CHUNKER_TARGET).aarch64.elf
	@echo "Valgrind tests completed"

# Valgrind testing for all tests (may show false positives from external libraries)
check-valgrind-all: $(ALL_TEST_TARGETS)
	@echo "Running all valgrind tests (including HTTP - may show false positives)..."
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MAIN_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_HTTP_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_ENV_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_OUTPUT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PROMPT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_VDB_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TOOLS_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_SHELL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_FILE_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_RALPH_TARGET).aarch64.elf
	@echo "All valgrind tests completed"

# =============================================================================
# DEPENDENCY MANAGEMENT
# =============================================================================

$(DEPDIR):
	mkdir -p $(DEPDIR)

# Download hnswlib (header-only library)
$(HNSWLIB_DIR)/hnswlib/hnswlib.h: | $(DEPDIR)
	@echo "Downloading hnswlib..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f hnswlib-$(HNSWLIB_VERSION).tar.gz ]; then \
		curl -L -o hnswlib-$(HNSWLIB_VERSION).tar.gz https://github.com/nmslib/hnswlib/archive/v$(HNSWLIB_VERSION).tar.gz || \
		wget -O hnswlib-$(HNSWLIB_VERSION).tar.gz https://github.com/nmslib/hnswlib/archive/v$(HNSWLIB_VERSION).tar.gz; \
	fi && \
	if [ ! -d hnswlib-$(HNSWLIB_VERSION) ]; then \
		tar -xzf hnswlib-$(HNSWLIB_VERSION).tar.gz; \
	fi

# Build MbedTLS libraries
$(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3): | $(DEPDIR)
	@echo "Building MbedTLS..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f mbedtls-$(MBEDTLS_VERSION).tar.gz ]; then \
		curl -L -o mbedtls-$(MBEDTLS_VERSION).tar.gz https://github.com/Mbed-TLS/mbedtls/archive/v$(MBEDTLS_VERSION).tar.gz || \
		wget -O mbedtls-$(MBEDTLS_VERSION).tar.gz https://github.com/Mbed-TLS/mbedtls/archive/v$(MBEDTLS_VERSION).tar.gz; \
	fi && \
	if [ ! -d mbedtls-$(MBEDTLS_VERSION) ]; then \
		tar -xzf mbedtls-$(MBEDTLS_VERSION).tar.gz; \
	fi && \
	cd mbedtls-$(MBEDTLS_VERSION) && \
	CC="$(CC)" CFLAGS="-O2" $(MAKE) lib

# Build libcurl (depends on MbedTLS)
$(CURL_LIB): $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	@echo "Building libcurl..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f curl-$(CURL_VERSION).tar.gz ]; then \
		curl -L -o curl-$(CURL_VERSION).tar.gz https://curl.se/download/curl-$(CURL_VERSION).tar.gz || \
		wget -O curl-$(CURL_VERSION).tar.gz https://curl.se/download/curl-$(CURL_VERSION).tar.gz; \
	fi && \
	if [ ! -d curl-$(CURL_VERSION) ]; then \
		tar -xzf curl-$(CURL_VERSION).tar.gz; \
	fi && \
	cd curl-$(CURL_VERSION) && \
	CC="$(CC)" LD="apelink" \
		CPPFLAGS="-D_GNU_SOURCE -I$$(pwd)/../mbedtls-$(MBEDTLS_VERSION)/include" \
		LDFLAGS="-L$$(pwd)/../mbedtls-$(MBEDTLS_VERSION)/library" \
		./configure \
		--disable-shared --enable-static \
		--disable-ldap --disable-sspi --disable-tls-srp --disable-rtsp \
		--disable-proxy --disable-dict --disable-telnet --disable-tftp \
		--disable-pop3 --disable-imap --disable-smb --disable-smtp \
		--disable-gopher --disable-manual --disable-ipv6 --disable-ftp \
		--disable-file --disable-ntlm --disable-crypto-auth --disable-digest-auth --disable-negotiate-auth --with-mbedtls --without-zlib --without-brotli \
		--without-zstd --without-libpsl --without-nghttp2 && \
	$(MAKE) CC="$(CC)"

# Build zlib library
$(ZLIB_LIB): | $(DEPDIR)
	@echo "Building zlib..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f zlib-$(ZLIB_VERSION).tar.gz ]; then \
		curl -L -o zlib-$(ZLIB_VERSION).tar.gz https://zlib.net/current/zlib.tar.gz || \
		wget -O zlib-$(ZLIB_VERSION).tar.gz https://zlib.net/current/zlib.tar.gz; \
	fi && \
	if [ ! -d zlib-$(ZLIB_VERSION) ]; then \
		tar -xzf zlib-$(ZLIB_VERSION).tar.gz; \
	fi && \
	cd zlib-$(ZLIB_VERSION) && \
	CC="$(CC)" CFLAGS="-O2" \
		./configure --static && \
	$(MAKE) CC="$(CC)"

# Build PDFio library
$(PDFIO_LIB): $(ZLIB_LIB) | $(DEPDIR)
	@echo "Building PDFio..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f pdfio-$(PDFIO_VERSION).tar.gz ]; then \
		curl -L -o pdfio-$(PDFIO_VERSION).tar.gz https://github.com/michaelrsweet/pdfio/archive/v$(PDFIO_VERSION).tar.gz || \
		wget -O pdfio-$(PDFIO_VERSION).tar.gz https://github.com/michaelrsweet/pdfio/archive/v$(PDFIO_VERSION).tar.gz; \
	fi && \
	if [ ! -d pdfio-$(PDFIO_VERSION) ]; then \
		tar -xzf pdfio-$(PDFIO_VERSION).tar.gz; \
	fi && \
	cd pdfio-$(PDFIO_VERSION) && \
	if [ ! -f Makefile ]; then \
		CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -I$$(pwd)/../zlib-$(ZLIB_VERSION)" LDFLAGS="-L$$(pwd)/../zlib-$(ZLIB_VERSION)" \
			./configure --disable-shared --enable-static; \
	fi && \
	if [ -f ../pdfio-ar-fix.patch ]; then patch -p0 < ../pdfio-ar-fix.patch || true; fi && \
	CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -I$$(pwd)/../zlib-$(ZLIB_VERSION)" LDFLAGS="-L$$(pwd)/../zlib-$(ZLIB_VERSION)" \
		$(MAKE) libpdfio.a

# Build cJSON library
$(CJSON_LIB): | $(DEPDIR)
	@echo "Building cJSON..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f cJSON-$(CJSON_VERSION).tar.gz ]; then \
		curl -L -o cJSON-$(CJSON_VERSION).tar.gz https://github.com/DaveGamble/cJSON/archive/v$(CJSON_VERSION).tar.gz || \
		wget -O cJSON-$(CJSON_VERSION).tar.gz https://github.com/DaveGamble/cJSON/archive/v$(CJSON_VERSION).tar.gz; \
	fi && \
	if [ ! -d cJSON-$(CJSON_VERSION) ]; then \
		tar -xzf cJSON-$(CJSON_VERSION).tar.gz; \
	fi && \
	cd cJSON-$(CJSON_VERSION) && \
	CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -fno-stack-protector" \
		$(MAKE) CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -fno-stack-protector" libcjson.a

# Build ncurses library
$(NCURSES_LIB): | $(DEPDIR)
	@echo "Building ncurses..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f ncurses-$(NCURSES_VERSION).tar.gz ]; then \
		curl -L -o ncurses-$(NCURSES_VERSION).tar.gz https://ftp.gnu.org/gnu/ncurses/ncurses-$(NCURSES_VERSION).tar.gz || \
		wget -O ncurses-$(NCURSES_VERSION).tar.gz https://ftp.gnu.org/gnu/ncurses/ncurses-$(NCURSES_VERSION).tar.gz; \
	fi && \
	if [ ! -d ncurses-$(NCURSES_VERSION) ]; then \
		tar -xzf ncurses-$(NCURSES_VERSION).tar.gz; \
	fi && \
	cd ncurses-$(NCURSES_VERSION) && \
	CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -fno-stack-protector" \
		./configure --disable-shared --enable-static --without-progs --without-tests --without-cxx-binding --without-ada && \
	CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -fno-stack-protector" \
		$(MAKE) CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib"

# Build Readline library
$(READLINE_LIB) $(HISTORY_LIB): $(NCURSES_LIB)
	@echo "Building Readline..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f readline-$(READLINE_VERSION).tar.gz ]; then \
		curl -L -o readline-$(READLINE_VERSION).tar.gz https://ftp.gnu.org/gnu/readline/readline-$(READLINE_VERSION).tar.gz || \
		wget -O readline-$(READLINE_VERSION).tar.gz https://ftp.gnu.org/gnu/readline/readline-$(READLINE_VERSION).tar.gz; \
	fi && \
	if [ ! -d readline-$(READLINE_VERSION) ]; then \
		tar -xzf readline-$(READLINE_VERSION).tar.gz; \
	fi && \
	cd readline-$(READLINE_VERSION) && \
	CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" \
		CFLAGS="-O2 -fno-stack-protector -I$$(pwd)/../ncurses-$(NCURSES_VERSION)/include" \
		LDFLAGS="-L$$(pwd)/../ncurses-$(NCURSES_VERSION)/lib" \
		./configure --disable-shared --enable-static && \
	CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" \
		CFLAGS="-O2 -fno-stack-protector -I$$(pwd)/../ncurses-$(NCURSES_VERSION)/include" \
		$(MAKE) CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" && \
	mkdir -p readline && \
	cd readline && \
	for f in ../*.h; do ln -sf "$$f" $$(basename "$$f"); done

# =============================================================================
# CLEAN TARGETS
# =============================================================================

clean:
	rm -f $(OBJECTS) $(TARGET) $(ALL_TEST_TARGETS)
	rm -f src/*.o src/*/*.o test/*.o test/*/*.o test/unity/*.o
	rm -f *.aarch64.elf *.com.dbg *.dbg src/*.aarch64.elf src/*/*.aarch64.elf src/*.com.dbg src/*/*.com.dbg src/*.dbg src/*/*.dbg test/*.aarch64.elf test/*/*.aarch64.elf test/*.com.dbg test/*/*.com.dbg test/*.dbg test/*/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log
	rm -f $(EMBEDDED_LINKS_HEADER)
	find build -type f ! -name 'bin2c.c' ! -name 'links' -delete 2>/dev/null || true

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz
	rm -f $(LINKS_BUNDLED)

.PHONY: all test check check-valgrind check-valgrind-all clean distclean