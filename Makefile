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
SQLITE_VERSION := 3450000
OSSP_UUID_VERSION := 1.6.2

# =============================================================================
# SOURCE FILES
# =============================================================================

# Core application sources
CORE_SOURCES := $(SRCDIR)/core/main.c \
                $(SRCDIR)/core/ralph.c \
                $(SRCDIR)/core/tool_executor.c \
                $(SRCDIR)/core/streaming_handler.c \
                $(SRCDIR)/core/context_enhancement.c \
                $(SRCDIR)/core/recap.c \
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
TOOL_SOURCES := $(SRCDIR)/tools/tools_system.c \
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
                 $(SRCDIR)/llm/models/response_processing.c \
                 $(SRCDIR)/llm/models/qwen_model.c \
                 $(SRCDIR)/llm/models/deepseek_model.c \
                 $(SRCDIR)/llm/models/gpt_model.c \
                 $(SRCDIR)/llm/models/claude_model.c \
                 $(SRCDIR)/llm/models/default_model.c

# Database and PDF
DB_C_SOURCES := $(SRCDIR)/db/vector_db.c $(SRCDIR)/db/vector_db_service.c $(SRCDIR)/db/metadata_store.c $(SRCDIR)/db/document_store.c $(SRCDIR)/db/task_store.c
DB_CPP_SOURCES := $(SRCDIR)/db/hnswlib_wrapper.cpp
PDF_SOURCES := $(SRCDIR)/pdf/pdf_extractor.c

# Utility sources (UUID, etc.)
UTILS_EXTRA_SOURCES := $(SRCDIR)/utils/uuid_utils.c

# CLI commands
CLI_SOURCES := $(SRCDIR)/cli/memory_commands.c

# Combined sources
C_SOURCES := $(CORE_SOURCES) $(TOOL_SOURCES) $(MCP_SOURCES) $(PROVIDER_SOURCES) $(MODEL_SOURCES) $(DB_C_SOURCES) $(PDF_SOURCES) $(CLI_SOURCES) $(UTILS_EXTRA_SOURCES)
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
SQLITE_DIR = $(DEPDIR)/sqlite-autoconf-$(SQLITE_VERSION)
OSSP_UUID_DIR = $(DEPDIR)/uuid-$(OSSP_UUID_VERSION)

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
SQLITE_LIB = $(SQLITE_DIR)/.libs/libsqlite3.a
OSSP_UUID_LIB = $(OSSP_UUID_DIR)/.libs/libuuid.a

# Python library
PYTHON_VERSION := 3.12
PYTHON_LIB := $(BUILDDIR)/libpython$(PYTHON_VERSION).a
PYTHON_INCLUDE := $(BUILDDIR)/python-include

# All required libraries
ALL_LIBS := $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(READLINE_LIB) $(HISTORY_LIB) $(NCURSES_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(PYTHON_LIB)

# =============================================================================
# STANDARD LIBRARY SETS FOR TEST LINKING
# =============================================================================

LIBS_MBEDTLS := $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
LIBS_STANDARD := $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(PYTHON_LIB) -lm -lpthread

# Include and library flags
INCLUDES = -I$(CURL_DIR)/include -I$(MBEDTLS_DIR)/include -I$(HNSWLIB_DIR) -I$(PDFIO_DIR) -I$(ZLIB_DIR) -I$(CJSON_DIR) -I$(READLINE_DIR) -I$(READLINE_DIR)/readline -I$(NCURSES_DIR)/include -I$(SQLITE_DIR) -I$(OSSP_UUID_DIR) -I$(PYTHON_INCLUDE) -I$(SRCDIR) -I$(SRCDIR)/core -I$(SRCDIR)/network -I$(SRCDIR)/llm -I$(SRCDIR)/session -I$(SRCDIR)/tools -I$(SRCDIR)/utils -I$(SRCDIR)/db -I$(SRCDIR)/pdf -I$(SRCDIR)/cli
TEST_INCLUDES = $(INCLUDES) -I$(TESTDIR)/unity -I$(TESTDIR) -I$(TESTDIR)/core -I$(TESTDIR)/network -I$(TESTDIR)/llm -I$(TESTDIR)/session -I$(TESTDIR)/tools -I$(TESTDIR)/utils
LDFLAGS = -L$(CURL_DIR)/lib/.libs -L$(MBEDTLS_DIR)/library -L$(PDFIO_DIR) -L$(ZLIB_DIR) -L$(CJSON_DIR) -L$(READLINE_DIR) -L$(NCURSES_DIR)/lib
LIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(READLINE_LIB) $(HISTORY_LIB) $(NCURSES_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(PYTHON_LIB) -lm

# =============================================================================
# TOOLS AND UTILITIES
# =============================================================================

BIN2C := $(BUILDDIR)/bin2c
LINKS_BUNDLED := $(BUILDDIR)/links
EMBEDDED_LINKS_HEADER := $(SRCDIR)/embedded_links.h

# CA Certificate bundle
CACERT_PEM := $(BUILDDIR)/cacert.pem
CACERT_SOURCE := $(SRCDIR)/network/embedded_cacert.c

# =============================================================================
# COMMON TEST COMPONENTS
# =============================================================================

COMMON_TEST_SOURCES := $(TESTDIR)/unity/unity.c
TOOL_TEST_DEPS := $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(SRCDIR)/tools/memory_tool.c $(SRCDIR)/tools/pdf_tool.c $(SRCDIR)/tools/tool_result_builder.c $(SRCDIR)/tools/subagent_tool.c $(SRCDIR)/tools/python_tool.c $(SRCDIR)/tools/python_tool_files.c
MODEL_TEST_DEPS := $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/response_processing.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c
UTIL_TEST_DEPS := $(SRCDIR)/utils/json_escape.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/json_output.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/utils/common_utils.c $(SRCDIR)/utils/document_chunker.c $(SRCDIR)/utils/pdf_processor.c $(SRCDIR)/utils/context_retriever.c $(SRCDIR)/utils/config.c $(SRCDIR)/utils/uuid_utils.c
NETWORK_TEST_DEPS := $(SRCDIR)/network/http_client.c $(SRCDIR)/network/embedded_cacert.c $(SRCDIR)/network/api_error.c
COMPLEX_TEST_DEPS := $(TOOL_TEST_DEPS) $(MODEL_TEST_DEPS) $(UTIL_TEST_DEPS) $(DB_C_SOURCES) $(SRCDIR)/pdf/pdf_extractor.c $(NETWORK_TEST_DEPS)

# Ralph core dependencies (for integration tests)
RALPH_CORE_DEPS := $(SRCDIR)/core/ralph.c $(SRCDIR)/core/tool_executor.c $(SRCDIR)/core/streaming_handler.c $(SRCDIR)/core/context_enhancement.c $(SRCDIR)/core/recap.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/network/api_common.c $(SRCDIR)/network/streaming.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/mcp/mcp_client.c

# Conversation-related test dependencies
CONV_DEPS := $(SRCDIR)/session/conversation_tracker.c $(DB_C_SOURCES) $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c $(SRCDIR)/network/http_client.c $(SRCDIR)/network/embedded_cacert.c $(SRCDIR)/network/api_error.c $(SRCDIR)/utils/json_escape.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/config.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/utils/common_utils.c

# =============================================================================
# TEST DEFINITION TEMPLATES
# =============================================================================

# Template: Simple C test (CC linker, no CPP)
# $(1) = test name, $(2) = test file, $(3) = additional sources
define SIMPLE_TEST
TEST_$(1)_SOURCES := $$(TESTDIR)/$(2).c $(3) $$(COMMON_TEST_SOURCES)
TEST_$(1)_OBJECTS := $$(TEST_$(1)_SOURCES:.c=.o)
TEST_$(1)_TARGET := $$(TESTDIR)/test_$(shell echo $(1) | tr A-Z a-z)
endef

# Template: Mixed C/C++ test (CXX linker)
# $(1) = test name, $(2) = test file, $(3) = additional C sources
define MIXED_TEST
TEST_$(1)_C_SOURCES := $$(TESTDIR)/$(2).c $(3) $$(COMMON_TEST_SOURCES)
TEST_$(1)_CPP_SOURCES := $$(DB_CPP_SOURCES)
TEST_$(1)_OBJECTS := $$(TEST_$(1)_C_SOURCES:.c=.o) $$(TEST_$(1)_CPP_SOURCES:.cpp=.o)
TEST_$(1)_TARGET := $$(TESTDIR)/test_$(shell echo $(1) | tr A-Z a-z)
endef

# =============================================================================
# TEST DEFINITIONS
# =============================================================================

# --- Minimal tests (no external libraries) ---
$(eval $(call SIMPLE_TEST,MAIN,core/test_main,))
$(eval $(call SIMPLE_TEST,ENV,utils/test_env_loader,$(SRCDIR)/utils/env_loader.c))
$(eval $(call SIMPLE_TEST,PROMPT,utils/test_prompt_loader,$(SRCDIR)/utils/prompt_loader.c))
$(eval $(call SIMPLE_TEST,TODO_MANAGER,tools/test_todo_manager,$(SRCDIR)/tools/todo_manager.c))
$(eval $(call SIMPLE_TEST,DOCUMENT_CHUNKER,test_document_chunker,$(SRCDIR)/utils/document_chunker.c $(SRCDIR)/utils/common_utils.c))
$(eval $(call SIMPLE_TEST,STREAMING,network/test_streaming,$(SRCDIR)/network/streaming.c))

# --- CJSON tests ---
$(eval $(call SIMPLE_TEST,CONFIG,utils/test_config,$(SRCDIR)/utils/config.c))
$(eval $(call SIMPLE_TEST,DEBUG_OUTPUT,utils/test_debug_output,$(SRCDIR)/utils/debug_output.c))

# --- PDF test ---
$(eval $(call SIMPLE_TEST,PDF_EXTRACTOR,pdf/test_pdf_extractor,$(SRCDIR)/pdf/pdf_extractor.c))

# --- HTTP retry test ---
$(eval $(call SIMPLE_TEST,HTTP_RETRY,network/test_http_retry,$(SRCDIR)/network/api_error.c $(SRCDIR)/utils/config.c))

# --- Conversation tests (with DB dependencies) ---
$(eval $(call SIMPLE_TEST,CONVERSATION,session/test_conversation_tracker,$(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/utils/json_escape.c))
$(eval $(call MIXED_TEST,CONVERSATION_VDB,session/test_conversation_vector_db,$(CONV_DEPS)))
$(eval $(call MIXED_TEST,TOOL_CALLS_NOT_STORED,session/test_tool_calls_not_stored,$(CONV_DEPS)))

# --- Standard mixed tests (with COMPLEX_TEST_DEPS) ---
$(eval $(call MIXED_TEST,JSON_OUTPUT,utils/test_json_output,$(SRCDIR)/network/streaming.c $(UTIL_TEST_DEPS) $(MODEL_TEST_DEPS) $(TOOL_TEST_DEPS) $(DB_C_SOURCES) $(NETWORK_TEST_DEPS) $(SRCDIR)/pdf/pdf_extractor.c))
$(eval $(call MIXED_TEST,TODO_TOOL,tools/test_todo_tool,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,OUTPUT,utils/test_output_formatter,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,TOOLS,tools/test_tools_system,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,VECTOR_DB_TOOL,tools/test_vector_db_tool,$(SRCDIR)/utils/env_loader.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,MEMORY_TOOL,tools/test_memory_tool,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,PYTHON_TOOL,tools/test_python_tool,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,PYTHON_INTEGRATION,tools/test_python_integration,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,MEMORY_MGMT,test_memory_management,$(SRCDIR)/cli/memory_commands.c $(DB_C_SOURCES) $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c $(SRCDIR)/utils/config.c $(NETWORK_TEST_DEPS) $(SRCDIR)/utils/common_utils.c $(SRCDIR)/utils/debug_output.c))
$(eval $(call MIXED_TEST,TOKEN_MANAGER,session/test_token_manager,$(SRCDIR)/session/token_manager.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/session/conversation_tracker.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,CONVERSATION_COMPACTOR,session/test_conversation_compactor,$(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/token_manager.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,MODEL_TOOLS,llm/test_model_tools,$(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,OPENAI_STREAMING,llm/test_openai_streaming,$(SRCDIR)/network/streaming.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/conversation_tracker.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,ANTHROPIC_STREAMING,llm/test_anthropic_streaming,$(SRCDIR)/network/streaming.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/conversation_tracker.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,MESSAGES_ARRAY_BUG,network/test_messages_array_bug,$(SRCDIR)/network/api_common.c $(SRCDIR)/session/conversation_tracker.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,MCP_CLIENT,mcp/test_mcp_client,$(RALPH_CORE_DEPS) $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,SUBAGENT_TOOL,tools/test_subagent_tool,$(RALPH_CORE_DEPS) $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,INCOMPLETE_TASK_BUG,core/test_incomplete_task_bug,$(RALPH_CORE_DEPS) $(COMPLEX_TEST_DEPS)))

# --- Complex integration tests (with full libraries) ---
$(eval $(call MIXED_TEST,HTTP,network/test_http_client,$(SRCDIR)/utils/env_loader.c $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,RALPH,core/test_ralph,$(TESTDIR)/mock_api_server.c $(RALPH_CORE_DEPS) $(COMPLEX_TEST_DEPS)))
$(eval $(call MIXED_TEST,RECAP,core/test_recap,$(RALPH_CORE_DEPS) $(COMPLEX_TEST_DEPS)))

# --- Special tests (unique dependencies) ---
# Vector DB test
TEST_VECTOR_DB_SOURCES := $(TESTDIR)/db/test_vector_db.c $(SRCDIR)/db/vector_db.c $(DB_CPP_SOURCES) $(TESTDIR)/unity/unity.c
TEST_VECTOR_DB_OBJECTS := $(TESTDIR)/db/test_vector_db.o $(SRCDIR)/db/vector_db.o $(SRCDIR)/db/hnswlib_wrapper.o $(TESTDIR)/unity/unity.o
TEST_VECTOR_DB_TARGET := $(TESTDIR)/test_vector_db

# Document store test
TEST_DOCUMENT_STORE_C_SOURCES := $(TESTDIR)/db/test_document_store.c $(DB_C_SOURCES) $(SRCDIR)/utils/common_utils.c $(SRCDIR)/utils/config.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/llm/embeddings_service.c $(SRCDIR)/llm/embedding_provider.c $(SRCDIR)/llm/providers/openai_embedding_provider.c $(SRCDIR)/llm/providers/local_embedding_provider.c $(NETWORK_TEST_DEPS) $(TESTDIR)/unity/unity.c
TEST_DOCUMENT_STORE_CPP_SOURCES := $(DB_CPP_SOURCES)
TEST_DOCUMENT_STORE_OBJECTS := $(TEST_DOCUMENT_STORE_C_SOURCES:.c=.o) $(TEST_DOCUMENT_STORE_CPP_SOURCES:.cpp=.o)
TEST_DOCUMENT_STORE_TARGET := $(TESTDIR)/test_document_store

# Task store test
$(eval $(call SIMPLE_TEST,TASK_STORE,db/test_task_store,$(SRCDIR)/db/task_store.c $(SRCDIR)/utils/uuid_utils.c))

# =============================================================================
# ALL TEST TARGETS
# =============================================================================

ALL_TEST_TARGETS := $(TEST_MAIN_TARGET) $(TEST_ENV_TARGET) $(TEST_CONFIG_TARGET) $(TEST_PROMPT_TARGET) \
    $(TEST_DEBUG_OUTPUT_TARGET) $(TEST_JSON_OUTPUT_TARGET) $(TEST_CONVERSATION_TARGET) \
    $(TEST_CONVERSATION_VDB_TARGET) $(TEST_TOOL_CALLS_NOT_STORED_TARGET) $(TEST_TODO_MANAGER_TARGET) \
    $(TEST_TODO_TOOL_TARGET) $(TEST_PDF_EXTRACTOR_TARGET) $(TEST_DOCUMENT_CHUNKER_TARGET) \
    $(TEST_HTTP_TARGET) $(TEST_HTTP_RETRY_TARGET) $(TEST_STREAMING_TARGET) \
    $(TEST_OPENAI_STREAMING_TARGET) $(TEST_ANTHROPIC_STREAMING_TARGET) $(TEST_OUTPUT_TARGET) \
    $(TEST_TOOLS_TARGET) $(TEST_VECTOR_DB_TOOL_TARGET) $(TEST_MEMORY_TOOL_TARGET) \
    $(TEST_PYTHON_TOOL_TARGET) $(TEST_PYTHON_INTEGRATION_TARGET) $(TEST_MEMORY_MGMT_TARGET) \
    $(TEST_TOKEN_MANAGER_TARGET) $(TEST_CONVERSATION_COMPACTOR_TARGET) $(TEST_RALPH_TARGET) \
    $(TEST_RECAP_TARGET) $(TEST_INCOMPLETE_TASK_BUG_TARGET) $(TEST_MODEL_TOOLS_TARGET) \
    $(TEST_MESSAGES_ARRAY_BUG_TARGET) $(TEST_VECTOR_DB_TARGET) $(TEST_DOCUMENT_STORE_TARGET) \
    $(TEST_TASK_STORE_TARGET) $(TEST_MCP_CLIENT_TARGET) $(TEST_SUBAGENT_TOOL_TARGET)

# =============================================================================
# BUILD RULES
# =============================================================================

PYTHON_STDLIB_DIR := python/build/results/py-tmp
PYTHON_DEFAULTS_DIR := src/tools/python_defaults

all: $(TARGET) embed-python

$(TARGET): $(OBJECTS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	@echo "Linking with PDFio support"
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS) -lpthread
	@echo "Saving base binary for smart embedding..."
	@uv run scripts/embed_python.py --save-base

embed-python: $(TARGET)
	@uv run scripts/embed_python.py

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

# Compilation rules (removed unnecessary $(ALL_LIBS) prerequisite)
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# =============================================================================
# TEST LINK RULES
# =============================================================================

# --- Minimal tests (no external libraries) ---
$(TEST_MAIN_TARGET): $(TEST_MAIN_OBJECTS)
	$(CC) -o $@ $(TEST_MAIN_OBJECTS)

$(TEST_ENV_TARGET): $(TEST_ENV_OBJECTS)
	$(CC) -o $@ $(TEST_ENV_OBJECTS)

$(TEST_PROMPT_TARGET): $(TEST_PROMPT_OBJECTS)
	$(CC) -o $@ $(TEST_PROMPT_OBJECTS)

$(TEST_TODO_MANAGER_TARGET): $(TEST_TODO_MANAGER_OBJECTS)
	$(CC) -o $@ $(TEST_TODO_MANAGER_OBJECTS)

$(TEST_DOCUMENT_CHUNKER_TARGET): $(TEST_DOCUMENT_CHUNKER_OBJECTS)
	$(CC) -o $@ $(TEST_DOCUMENT_CHUNKER_OBJECTS)

$(TEST_STREAMING_TARGET): $(TEST_STREAMING_OBJECTS)
	$(CC) -o $@ $(TEST_STREAMING_OBJECTS)

# --- CJSON tests ---
$(TEST_CONFIG_TARGET): $(TEST_CONFIG_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_CONFIG_OBJECTS) $(CJSON_LIB)

$(TEST_DEBUG_OUTPUT_TARGET): $(TEST_DEBUG_OUTPUT_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_DEBUG_OUTPUT_OBJECTS) $(CJSON_LIB)

# --- PDF test ---
$(TEST_PDF_EXTRACTOR_TARGET): $(TEST_PDF_EXTRACTOR_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB)
	$(CC) -o $@ $(TEST_PDF_EXTRACTOR_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB) -lm

# --- HTTP retry test ---
$(TEST_HTTP_RETRY_TARGET): $(TEST_HTTP_RETRY_OBJECTS) $(CJSON_LIB) $(LIBS_MBEDTLS)
	$(CC) -o $@ $(TEST_HTTP_RETRY_OBJECTS) $(CJSON_LIB) $(LIBS_MBEDTLS) -lm

# --- Conversation test (special linking) ---
$(TEST_CONVERSATION_TARGET): $(TEST_CONVERSATION_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_CONVERSATION_OBJECTS) $(DB_C_SOURCES:.c=.o) $(DB_CPP_SOURCES:.cpp=.o) \
		src/llm/embeddings.o src/llm/embeddings_service.o src/llm/embedding_provider.o \
		src/llm/providers/openai_embedding_provider.o src/llm/providers/local_embedding_provider.o \
		src/network/http_client.o src/network/embedded_cacert.o src/network/api_error.o \
		src/utils/env_loader.o src/utils/config.o src/utils/debug_output.o src/utils/common_utils.o \
		$(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

# --- Standard mixed tests (CXX with LIBS_STANDARD) ---
STANDARD_TESTS := JSON_OUTPUT TODO_TOOL OUTPUT TOOLS VECTOR_DB_TOOL MEMORY_TOOL \
    MEMORY_MGMT TOKEN_MANAGER CONVERSATION_COMPACTOR MODEL_TOOLS OPENAI_STREAMING \
    ANTHROPIC_STREAMING MESSAGES_ARRAY_BUG MCP_CLIENT SUBAGENT_TOOL INCOMPLETE_TASK_BUG \
    CONVERSATION_VDB TOOL_CALLS_NOT_STORED

$(TEST_JSON_OUTPUT_TARGET): $(TEST_JSON_OUTPUT_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_JSON_OUTPUT_OBJECTS) $(LIBS_STANDARD)

$(TEST_TODO_TOOL_TARGET): $(TEST_TODO_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_TODO_TOOL_OBJECTS) $(LIBS_STANDARD)

$(TEST_OUTPUT_TARGET): $(TEST_OUTPUT_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_OUTPUT_OBJECTS) src/session/conversation_tracker.o $(LIBS_STANDARD)

$(TEST_TOOLS_TARGET): $(TEST_TOOLS_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_TOOLS_OBJECTS) src/session/conversation_tracker.o $(LIBS_STANDARD)

$(TEST_VECTOR_DB_TOOL_TARGET): $(TEST_VECTOR_DB_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_VECTOR_DB_TOOL_OBJECTS) $(LIBS_STANDARD)

$(TEST_MEMORY_TOOL_TARGET): $(TEST_MEMORY_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MEMORY_TOOL_OBJECTS) $(LIBS_STANDARD)

$(TEST_MEMORY_MGMT_TARGET): $(TEST_MEMORY_MGMT_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MEMORY_MGMT_OBJECTS) $(LIBS_STANDARD)

$(TEST_TOKEN_MANAGER_TARGET): $(TEST_TOKEN_MANAGER_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_TOKEN_MANAGER_OBJECTS) $(LIBS_STANDARD)

$(TEST_CONVERSATION_COMPACTOR_TARGET): $(TEST_CONVERSATION_COMPACTOR_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_CONVERSATION_COMPACTOR_OBJECTS) $(LIBS_STANDARD)

$(TEST_MODEL_TOOLS_TARGET): $(TEST_MODEL_TOOLS_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MODEL_TOOLS_OBJECTS) $(LIBS_STANDARD)

$(TEST_OPENAI_STREAMING_TARGET): $(TEST_OPENAI_STREAMING_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_OPENAI_STREAMING_OBJECTS) $(LIBS_STANDARD)

$(TEST_ANTHROPIC_STREAMING_TARGET): $(TEST_ANTHROPIC_STREAMING_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_ANTHROPIC_STREAMING_OBJECTS) $(LIBS_STANDARD)

$(TEST_MESSAGES_ARRAY_BUG_TARGET): $(TEST_MESSAGES_ARRAY_BUG_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MESSAGES_ARRAY_BUG_OBJECTS) $(LIBS_STANDARD)

$(TEST_MCP_CLIENT_TARGET): $(TEST_MCP_CLIENT_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_MCP_CLIENT_OBJECTS) $(LIBS_STANDARD)

$(TEST_SUBAGENT_TOOL_TARGET): $(TEST_SUBAGENT_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_SUBAGENT_TOOL_OBJECTS) $(LIBS_STANDARD)

$(TEST_INCOMPLETE_TASK_BUG_TARGET): $(TEST_INCOMPLETE_TASK_BUG_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_INCOMPLETE_TASK_BUG_OBJECTS) $(LIBS_STANDARD)

$(TEST_CONVERSATION_VDB_TARGET): $(TEST_CONVERSATION_VDB_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_CONVERSATION_VDB_OBJECTS) $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_TOOL_CALLS_NOT_STORED_TARGET): $(TEST_TOOL_CALLS_NOT_STORED_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_TOOL_CALLS_NOT_STORED_OBJECTS) $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

# --- Python tests (with stdlib embedding) ---
define PYTHON_TEST_EMBED
	@set -e; \
	if [ ! -d "$(PYTHON_STDLIB_DIR)/lib" ]; then \
		echo "Error: Python stdlib directory '$(PYTHON_STDLIB_DIR)/lib' not found."; \
		exit 1; \
	fi; \
	echo "Embedding Python stdlib into test binary..."; \
	rm -f $(BUILDDIR)/stdlib.zip; \
	cd $(PYTHON_STDLIB_DIR) && zip -qr $(CURDIR)/$(BUILDDIR)/stdlib.zip lib/; \
	zipcopy $(CURDIR)/$(BUILDDIR)/stdlib.zip $(CURDIR)/$@; \
	rm -f $(BUILDDIR)/stdlib.zip
endef

$(TEST_PYTHON_TOOL_TARGET): $(TEST_PYTHON_TOOL_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_PYTHON_TOOL_OBJECTS) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

$(TEST_PYTHON_INTEGRATION_TARGET): $(TEST_PYTHON_INTEGRATION_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_PYTHON_INTEGRATION_OBJECTS) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

# --- Complex integration tests (with full LIBS) ---
$(TEST_HTTP_TARGET): $(TEST_HTTP_OBJECTS) $(ALL_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_HTTP_OBJECTS) $(LIBS)

$(TEST_RALPH_TARGET): $(TEST_RALPH_OBJECTS) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_RALPH_OBJECTS) $(LIBS) -lpthread

$(TEST_RECAP_TARGET): $(TEST_RECAP_OBJECTS) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_RECAP_OBJECTS) $(LIBS) -lpthread

# --- Special tests ---
$(TEST_VECTOR_DB_TARGET): $(TEST_VECTOR_DB_OBJECTS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) -o $@ $(TEST_VECTOR_DB_OBJECTS) -lpthread -lm

$(TEST_DOCUMENT_STORE_TARGET): $(TEST_DOCUMENT_STORE_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_DOCUMENT_STORE_OBJECTS) $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lm -lpthread

$(TEST_TASK_STORE_TARGET): $(TEST_TASK_STORE_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB)
	$(CC) -o $@ $(TEST_TASK_STORE_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lpthread -lm

# =============================================================================
# TEST EXECUTION
# =============================================================================

test: $(ALL_TEST_TARGETS)
	@echo "Running all tests..."
	@for t in $(TEST_MAIN_TARGET) $(TEST_HTTP_TARGET) $(TEST_HTTP_RETRY_TARGET) \
	    $(TEST_STREAMING_TARGET) $(TEST_OPENAI_STREAMING_TARGET) $(TEST_ANTHROPIC_STREAMING_TARGET) \
	    $(TEST_ENV_TARGET) $(TEST_OUTPUT_TARGET) $(TEST_PROMPT_TARGET) $(TEST_DEBUG_OUTPUT_TARGET) \
	    $(TEST_CONVERSATION_TARGET) $(TEST_CONVERSATION_VDB_TARGET) $(TEST_TOOLS_TARGET) \
	    $(TEST_RALPH_TARGET) $(TEST_TODO_MANAGER_TARGET) $(TEST_TODO_TOOL_TARGET) \
	    $(TEST_VECTOR_DB_TOOL_TARGET) $(TEST_MEMORY_TOOL_TARGET) $(TEST_PYTHON_TOOL_TARGET) \
	    $(TEST_PYTHON_INTEGRATION_TARGET) $(TEST_TOKEN_MANAGER_TARGET) $(TEST_MODEL_TOOLS_TARGET) \
	    $(TEST_CONVERSATION_COMPACTOR_TARGET) $(TEST_INCOMPLETE_TASK_BUG_TARGET) \
	    $(TEST_MESSAGES_ARRAY_BUG_TARGET) $(TEST_MCP_CLIENT_TARGET) $(TEST_VECTOR_DB_TARGET) \
	    $(TEST_DOCUMENT_STORE_TARGET) $(TEST_TASK_STORE_TARGET) $(TEST_PDF_EXTRACTOR_TARGET) \
	    $(TEST_DOCUMENT_CHUNKER_TARGET) $(TEST_SUBAGENT_TOOL_TARGET) $(TEST_JSON_OUTPUT_TARGET); do \
		./$$t || exit 1; \
	done
	@echo "All tests completed"

check: test

# Valgrind test list (excludes HTTP, Python, and subagent tests)
VALGRIND_TESTS := $(TEST_MAIN_TARGET) $(TEST_HTTP_RETRY_TARGET) $(TEST_STREAMING_TARGET) \
    $(TEST_OPENAI_STREAMING_TARGET) $(TEST_ANTHROPIC_STREAMING_TARGET) $(TEST_ENV_TARGET) \
    $(TEST_OUTPUT_TARGET) $(TEST_PROMPT_TARGET) $(TEST_CONVERSATION_TARGET) \
    $(TEST_CONVERSATION_VDB_TARGET) $(TEST_TOOLS_TARGET) $(TEST_RALPH_TARGET) \
    $(TEST_TODO_MANAGER_TARGET) $(TEST_TODO_TOOL_TARGET) $(TEST_VECTOR_DB_TOOL_TARGET) \
    $(TEST_MEMORY_TOOL_TARGET) $(TEST_TOKEN_MANAGER_TARGET) $(TEST_CONVERSATION_COMPACTOR_TARGET) \
    $(TEST_MODEL_TOOLS_TARGET) $(TEST_VECTOR_DB_TARGET) $(TEST_TASK_STORE_TARGET) \
    $(TEST_PDF_EXTRACTOR_TARGET) $(TEST_DOCUMENT_CHUNKER_TARGET) $(TEST_JSON_OUTPUT_TARGET)

VALGRIND_FLAGS := --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1

check-valgrind: $(ALL_TEST_TARGETS)
	@echo "Running valgrind tests (excluding HTTP and Python tests)..."
	@for t in $(VALGRIND_TESTS); do \
		valgrind $(VALGRIND_FLAGS) ./$$t.aarch64.elf || exit 1; \
	done
	@echo "Valgrind tests completed (subagent tests excluded - see AGENTS.md)"

check-valgrind-all: $(ALL_TEST_TARGETS)
	@echo "Running all valgrind tests (including HTTP - may show false positives)..."
	@for t in $(TEST_MAIN_TARGET) $(TEST_HTTP_TARGET) $(TEST_ENV_TARGET) $(TEST_OUTPUT_TARGET) \
	    $(TEST_PROMPT_TARGET) $(TEST_CONVERSATION_TARGET) $(TEST_CONVERSATION_VDB_TARGET) \
	    $(TEST_TOOLS_TARGET) $(TEST_RALPH_TARGET); do \
		valgrind $(VALGRIND_FLAGS) ./$$t.aarch64.elf || exit 1; \
	done
	@echo "All valgrind tests completed"

# =============================================================================
# DEPENDENCY MANAGEMENT
# =============================================================================

$(DEPDIR):
	mkdir -p $(DEPDIR)

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
	sed -i 's|^AR[[:space:]]*=.*|AR\t\t=\tcosmoar|' Makefile && \
	CC="$(CC)" AR="cosmoar" RANLIB="cosmoranlib" CFLAGS="-O2 -I$$(pwd)/../zlib-$(ZLIB_VERSION)" LDFLAGS="-L$$(pwd)/../zlib-$(ZLIB_VERSION)" \
		$(MAKE) libpdfio.a

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

$(SQLITE_LIB): | $(DEPDIR)
	@echo "Building SQLite..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f sqlite-autoconf-$(SQLITE_VERSION).tar.gz ]; then \
		curl -L -o sqlite-autoconf-$(SQLITE_VERSION).tar.gz https://www.sqlite.org/2024/sqlite-autoconf-$(SQLITE_VERSION).tar.gz || \
		wget -O sqlite-autoconf-$(SQLITE_VERSION).tar.gz https://www.sqlite.org/2024/sqlite-autoconf-$(SQLITE_VERSION).tar.gz; \
	fi && \
	if [ ! -d sqlite-autoconf-$(SQLITE_VERSION) ]; then \
		tar -xzf sqlite-autoconf-$(SQLITE_VERSION).tar.gz; \
	fi && \
	cd sqlite-autoconf-$(SQLITE_VERSION) && \
	$(CC) -O2 -fno-stack-protector \
		-DSQLITE_THREADSAFE=1 \
		-DSQLITE_ENABLE_FTS5 \
		-DSQLITE_ENABLE_JSON1 \
		-DSQLITE_ENABLE_RTREE \
		-DSQLITE_ENABLE_MATH_FUNCTIONS \
		-DSQLITE_DQS=0 \
		-c sqlite3.c -o sqlite3.o && \
	mkdir -p .libs && \
	cosmoar rcs .libs/libsqlite3.a sqlite3.o && \
	aarch64-linux-cosmo-ranlib .libs/libsqlite3.a

$(OSSP_UUID_LIB): | $(DEPDIR)
	@echo "Building OSSP UUID..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f uuid-$(OSSP_UUID_VERSION).tar.gz ]; then \
		curl -L -o uuid-$(OSSP_UUID_VERSION).tar.gz https://deb.debian.org/debian/pool/main/o/ossp-uuid/ossp-uuid_$(OSSP_UUID_VERSION).orig.tar.gz || \
		wget -O uuid-$(OSSP_UUID_VERSION).tar.gz https://deb.debian.org/debian/pool/main/o/ossp-uuid/ossp-uuid_$(OSSP_UUID_VERSION).orig.tar.gz; \
	fi && \
	if [ ! -d uuid-$(OSSP_UUID_VERSION) ]; then \
		tar -xzf uuid-$(OSSP_UUID_VERSION).tar.gz; \
	fi && \
	cd uuid-$(OSSP_UUID_VERSION) && \
	cp /usr/share/misc/config.guess . && \
	cp /usr/share/misc/config.sub . && \
	CC="$(CC)" LD="apelink" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -fno-stack-protector" \
		./configure --disable-shared --enable-static --without-perl --without-php --without-pgsql && \
	CC="$(CC)" LD="apelink" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib" CFLAGS="-O2 -fno-stack-protector" \
		$(MAKE) CC="$(CC)" AR="cosmoar" RANLIB="aarch64-linux-cosmo-ranlib"

# =============================================================================
# PYTHON BUILD
# =============================================================================

$(PYTHON_LIB): $(ZLIB_LIB)
	@echo "Building Python..."
	$(MAKE) -C python
	@test -f $(PYTHON_LIB) || (echo "Error: Python build did not produce $(PYTHON_LIB)" && exit 1)

python: $(PYTHON_LIB)

# =============================================================================
# CA CERTIFICATE BUNDLE
# =============================================================================

$(CACERT_PEM):
	@echo "Downloading Mozilla CA certificate bundle..."
	@mkdir -p $(BUILDDIR)
	curl -sL https://curl.se/ca/cacert.pem -o $(CACERT_PEM)
	@echo "Downloaded CA bundle ($$(wc -c < $(CACERT_PEM) | tr -d ' ') bytes)"

$(CACERT_SOURCE): $(CACERT_PEM)
	@echo "Generating embedded CA certificate source..."
	./scripts/gen_cacert.sh $(CACERT_PEM) $(CACERT_SOURCE)

update-cacert:
	@echo "Updating Mozilla CA certificate bundle..."
	@mkdir -p $(BUILDDIR)
	curl -sL https://curl.se/ca/cacert.pem -o $(CACERT_PEM)
	./scripts/gen_cacert.sh $(CACERT_PEM) $(CACERT_SOURCE)
	@echo "CA certificate bundle updated. Rebuild with 'make clean && make'"

# =============================================================================
# CLEAN TARGETS
# =============================================================================

clean:
	rm -f $(OBJECTS) $(TARGET) $(ALL_TEST_TARGETS)
	rm -f src/*.o src/*/*.o test/*.o test/*/*.o test/unity/*.o
	rm -f *.aarch64.elf *.com.dbg *.dbg src/*.aarch64.elf src/*/*.aarch64.elf src/*.com.dbg src/*/*.com.dbg src/*.dbg src/*/*.dbg test/*.aarch64.elf test/*/*.aarch64.elf test/*.com.dbg test/*/*.com.dbg test/*.dbg test/*/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log
	rm -f $(EMBEDDED_LINKS_HEADER)
	find build -type f ! -name 'bin2c.c' ! -name 'links' ! -name 'libpython*.a' ! -path 'build/python-include/*' -delete 2>/dev/null || true

clean-python:
	rm -f $(PYTHON_LIB)
	rm -rf $(PYTHON_INCLUDE)
	$(MAKE) -C python clean

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz
	rm -f $(LINKS_BUNDLED)
	$(MAKE) -C python distclean

.PHONY: all test check check-valgrind check-valgrind-all clean clean-python distclean python embed-python update-cacert
