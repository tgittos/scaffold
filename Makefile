# Modern Makefile for ralph HTTP client
# Built with Cosmopolitan for universal binary compatibility

# =============================================================================
# CONFIGURATION
# =============================================================================

# Compiler and build settings
CC := cosmocc
CXX := cosmoc++
CFLAGS := -Wall -Wextra -Werror -O2 -std=c11
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

# =============================================================================
# SOURCE FILES
# =============================================================================

# Core source files
CORE_SOURCES := $(SRCDIR)/core/main.c \
                $(SRCDIR)/core/ralph.c \
                $(SRCDIR)/network/http_client.c \
                $(SRCDIR)/utils/env_loader.c \
                $(SRCDIR)/utils/output_formatter.c \
                $(SRCDIR)/utils/prompt_loader.c \
                $(SRCDIR)/session/conversation_tracker.c \
                $(SRCDIR)/session/conversation_compactor.c \
                $(SRCDIR)/session/session_manager.c \
                $(SRCDIR)/utils/debug_output.c \
                $(SRCDIR)/network/api_common.c \
                $(SRCDIR)/utils/json_utils.c \
                $(SRCDIR)/session/token_manager.c \
                $(SRCDIR)/llm/llm_provider.c

# Tool system sources
TOOL_SOURCES := $(SRCDIR)/tools/tools_system.c \
                $(SRCDIR)/tools/shell_tool.c \
                $(SRCDIR)/tools/file_tools.c \
                $(SRCDIR)/tools/links_tool.c \
                $(SRCDIR)/tools/todo_manager.c \
                $(SRCDIR)/tools/todo_tool.c \
                $(SRCDIR)/tools/todo_display.c \
                $(SRCDIR)/tools/vector_db_tool.c

# Provider sources
PROVIDER_SOURCES := $(SRCDIR)/llm/providers/openai_provider.c \
                    $(SRCDIR)/llm/providers/anthropic_provider.c \
                    $(SRCDIR)/llm/providers/local_ai_provider.c \
                    $(SRCDIR)/llm/embeddings.c

# Model sources
MODEL_SOURCES := $(SRCDIR)/llm/model_capabilities.c \
                 $(SRCDIR)/llm/models/qwen_model.c \
                 $(SRCDIR)/llm/models/deepseek_model.c \
                 $(SRCDIR)/llm/models/gpt_model.c \
                 $(SRCDIR)/llm/models/claude_model.c \
                 $(SRCDIR)/llm/models/default_model.c

# Database sources
DB_C_SOURCES := $(SRCDIR)/db/vector_db.c
DB_CPP_SOURCES := $(SRCDIR)/db/hnswlib_wrapper.cpp
DB_SOURCES := $(DB_C_SOURCES) $(DB_CPP_SOURCES)

# All sources combined
C_SOURCES := $(CORE_SOURCES) $(TOOL_SOURCES) $(PROVIDER_SOURCES) $(MODEL_SOURCES) $(SRCDIR)/db/vector_db.c
CPP_SOURCES := $(SRCDIR)/db/hnswlib_wrapper.cpp
SOURCES := $(C_SOURCES) $(CPP_SOURCES)
OBJECTS := $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)

# Header files
HEADERS := $(wildcard $(SRCDIR)/*/*.h) $(SRCDIR)/embedded_links.h

# =============================================================================
# TOOLS AND UTILITIES
# =============================================================================

BIN2C := $(BUILDDIR)/bin2c
LINKS_BUNDLED := $(BUILDDIR)/links
EMBEDDED_LINKS_HEADER := $(SRCDIR)/embedded_links.h

# Test files
TEST_MAIN_SOURCES = $(TESTDIR)/core/test_main.c $(TESTDIR)/unity/unity.c
TEST_MAIN_OBJECTS = $(TEST_MAIN_SOURCES:.c=.o)
TEST_MAIN_TARGET = $(TESTDIR)/test_main

TEST_HTTP_SOURCES = $(TESTDIR)/network/test_http_client.c $(SRCDIR)/network/http_client.c $(SRCDIR)/utils/env_loader.c $(TESTDIR)/unity/unity.c
TEST_HTTP_OBJECTS = $(TEST_HTTP_SOURCES:.c=.o)
TEST_HTTP_TARGET = $(TESTDIR)/test_http_client

TEST_ENV_SOURCES = $(TESTDIR)/utils/test_env_loader.c $(SRCDIR)/utils/env_loader.c $(TESTDIR)/unity/unity.c
TEST_ENV_OBJECTS = $(TEST_ENV_SOURCES:.c=.o)
TEST_ENV_TARGET = $(TESTDIR)/test_env_loader

TEST_OUTPUT_C_SOURCES = $(TESTDIR)/utils/test_output_formatter.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_OUTPUT_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_OUTPUT_OBJECTS = $(TEST_OUTPUT_C_SOURCES:.c=.o) $(TEST_OUTPUT_CPP_SOURCES:.cpp=.o)
TEST_OUTPUT_TARGET = $(TESTDIR)/test_output_formatter

TEST_PROMPT_SOURCES = $(TESTDIR)/utils/test_prompt_loader.c $(SRCDIR)/utils/prompt_loader.c $(TESTDIR)/unity/unity.c
TEST_PROMPT_OBJECTS = $(TEST_PROMPT_SOURCES:.c=.o)
TEST_PROMPT_TARGET = $(TESTDIR)/test_prompt_loader

TEST_CONVERSATION_SOURCES = $(TESTDIR)/session/test_conversation_tracker.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/utils/json_utils.c $(TESTDIR)/unity/unity.c
TEST_CONVERSATION_OBJECTS = $(TEST_CONVERSATION_SOURCES:.c=.o)
TEST_CONVERSATION_TARGET = $(TESTDIR)/test_conversation_tracker

TEST_TOOLS_C_SOURCES = $(TESTDIR)/tools/test_tools_system.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_TOOLS_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_TOOLS_OBJECTS = $(TEST_TOOLS_C_SOURCES:.c=.o) $(TEST_TOOLS_CPP_SOURCES:.cpp=.o)
TEST_TOOLS_TARGET = $(TESTDIR)/test_tools_system

TEST_SHELL_C_SOURCES = $(TESTDIR)/tools/test_shell_tool.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_SHELL_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_SHELL_OBJECTS = $(TEST_SHELL_C_SOURCES:.c=.o) $(TEST_SHELL_CPP_SOURCES:.cpp=.o)
TEST_SHELL_TARGET = $(TESTDIR)/test_shell_tool

TEST_FILE_C_SOURCES = $(TESTDIR)/tools/test_file_tools.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_FILE_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_FILE_OBJECTS = $(TEST_FILE_C_SOURCES:.c=.o) $(TEST_FILE_CPP_SOURCES:.cpp=.o)
TEST_FILE_TARGET = $(TESTDIR)/test_file_tools

TEST_SMART_FILE_C_SOURCES = $(TESTDIR)/tools/test_smart_file_tools.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_SMART_FILE_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_SMART_FILE_OBJECTS = $(TEST_SMART_FILE_C_SOURCES:.c=.o) $(TEST_SMART_FILE_CPP_SOURCES:.cpp=.o)
TEST_SMART_FILE_TARGET = $(TESTDIR)/test_smart_file_tools

TEST_RALPH_C_SOURCES = $(TESTDIR)/core/test_ralph.c $(TESTDIR)/mock_api_server.c $(SRCDIR)/core/ralph.c $(SRCDIR)/network/http_client.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/network/api_common.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(TESTDIR)/unity/unity.c
TEST_RALPH_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_RALPH_OBJECTS = $(TEST_RALPH_C_SOURCES:.c=.o) $(TEST_RALPH_CPP_SOURCES:.cpp=.o)
TEST_RALPH_TARGET = $(TESTDIR)/test_ralph

TEST_BUNDLED_LINKS = test_bundled_links

TEST_TODO_MANAGER_SOURCES = $(TESTDIR)/tools/test_todo_manager.c $(SRCDIR)/tools/todo_manager.c $(TESTDIR)/unity/unity.c
TEST_TODO_MANAGER_OBJECTS = $(TEST_TODO_MANAGER_SOURCES:.c=.o)
TEST_TODO_MANAGER_TARGET = $(TESTDIR)/test_todo_manager

TEST_TODO_TOOL_SOURCES = $(TESTDIR)/tools/test_todo_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(TESTDIR)/unity/unity.c
TEST_TODO_TOOL_OBJECTS = $(TEST_TODO_TOOL_SOURCES:.c=.o)
TEST_TODO_TOOL_TARGET = $(TESTDIR)/test_todo_tool

TEST_VECTOR_DB_TOOL_C_SOURCES = $(TESTDIR)/tools/test_vector_db_tool.c $(SRCDIR)/tools/vector_db_tool.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/db/vector_db.c $(SRCDIR)/utils/json_utils.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_VECTOR_DB_TOOL_CPP_SOURCES = $(SRCDIR)/db/hnswlib_wrapper.cpp
TEST_VECTOR_DB_TOOL_OBJECTS = $(TEST_VECTOR_DB_TOOL_C_SOURCES:.c=.o) $(TEST_VECTOR_DB_TOOL_CPP_SOURCES:.cpp=.o)
TEST_VECTOR_DB_TOOL_TARGET = $(TESTDIR)/test_vector_db_tool


TEST_TOKEN_MANAGER_SOURCES = $(TESTDIR)/session/test_token_manager.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/utils/json_utils.c $(SRCDIR)/network/http_client.c $(SRCDIR)/utils/debug_output.c $(TESTDIR)/unity/unity.c
TEST_TOKEN_MANAGER_OBJECTS = $(TEST_TOKEN_MANAGER_SOURCES:.c=.o)
TEST_TOKEN_MANAGER_TARGET = $(TESTDIR)/test_token_manager

TEST_CONVERSATION_COMPACTOR_SOURCES = $(TESTDIR)/session/test_conversation_compactor.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/utils/json_utils.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_CONVERSATION_COMPACTOR_OBJECTS = $(TEST_CONVERSATION_COMPACTOR_SOURCES:.c=.o)
TEST_CONVERSATION_COMPACTOR_TARGET = $(TESTDIR)/test_conversation_compactor

TEST_INCOMPLETE_TASK_BUG_C_SOURCES = $(TESTDIR)/core/test_incomplete_task_bug.c $(SRCDIR)/core/ralph.c $(SRCDIR)/network/http_client.c $(SRCDIR)/utils/env_loader.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/session/conversation_compactor.c $(SRCDIR)/session/session_manager.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/network/api_common.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/session/token_manager.c $(SRCDIR)/llm/llm_provider.c $(SRCDIR)/llm/providers/openai_provider.c $(SRCDIR)/llm/providers/anthropic_provider.c $(SRCDIR)/llm/providers/local_ai_provider.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(TESTDIR)/unity/unity.c
TEST_INCOMPLETE_TASK_BUG_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_INCOMPLETE_TASK_BUG_OBJECTS = $(TEST_INCOMPLETE_TASK_BUG_C_SOURCES:.c=.o) $(TEST_INCOMPLETE_TASK_BUG_CPP_SOURCES:.cpp=.o)
TEST_INCOMPLETE_TASK_BUG_TARGET = $(TESTDIR)/test_incomplete_task_bug

TEST_MODEL_TOOLS_C_SOURCES = $(TESTDIR)/llm/test_model_tools.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/utils/json_utils.c $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_MODEL_TOOLS_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MODEL_TOOLS_OBJECTS = $(TEST_MODEL_TOOLS_C_SOURCES:.c=.o) $(TEST_MODEL_TOOLS_CPP_SOURCES:.cpp=.o)
TEST_MODEL_TOOLS_TARGET = $(TESTDIR)/test_model_tools

TEST_MESSAGES_ARRAY_BUG_C_SOURCES = $(TESTDIR)/network/test_messages_array_bug.c $(SRCDIR)/network/api_common.c $(SRCDIR)/session/conversation_tracker.c $(SRCDIR)/utils/json_utils.c $(SRCDIR)/llm/model_capabilities.c $(SRCDIR)/tools/tools_system.c $(SRCDIR)/utils/output_formatter.c $(SRCDIR)/utils/debug_output.c $(SRCDIR)/tools/shell_tool.c $(SRCDIR)/tools/file_tools.c $(SRCDIR)/tools/links_tool.c $(SRCDIR)/tools/todo_tool.c $(SRCDIR)/tools/todo_manager.c $(SRCDIR)/tools/todo_display.c $(SRCDIR)/tools/vector_db_tool.c $(DB_C_SOURCES) $(SRCDIR)/llm/models/qwen_model.c $(SRCDIR)/llm/models/deepseek_model.c $(SRCDIR)/llm/models/gpt_model.c $(SRCDIR)/llm/models/claude_model.c $(SRCDIR)/llm/models/default_model.c $(SRCDIR)/llm/embeddings.c $(SRCDIR)/network/http_client.c $(TESTDIR)/unity/unity.c
TEST_MESSAGES_ARRAY_BUG_CPP_SOURCES = $(DB_CPP_SOURCES)
TEST_MESSAGES_ARRAY_BUG_OBJECTS = $(TEST_MESSAGES_ARRAY_BUG_C_SOURCES:.c=.o) $(TEST_MESSAGES_ARRAY_BUG_CPP_SOURCES:.cpp=.o)
TEST_MESSAGES_ARRAY_BUG_TARGET = $(TESTDIR)/test_messages_array_bug

TEST_VECTOR_DB_SOURCES = $(TESTDIR)/db/test_vector_db.c $(SRCDIR)/db/vector_db.c $(SRCDIR)/db/hnswlib_wrapper.cpp $(TESTDIR)/unity/unity.c
TEST_VECTOR_DB_C_OBJECTS = $(TESTDIR)/db/test_vector_db.o $(SRCDIR)/db/vector_db.o $(TESTDIR)/unity/unity.o
TEST_VECTOR_DB_CPP_OBJECTS = $(SRCDIR)/db/hnswlib_wrapper.o
TEST_VECTOR_DB_OBJECTS = $(TEST_VECTOR_DB_C_OBJECTS) $(TEST_VECTOR_DB_CPP_OBJECTS)
TEST_VECTOR_DB_TARGET = $(TESTDIR)/test_vector_db

ALL_TEST_TARGETS = $(TEST_MAIN_TARGET) $(TEST_HTTP_TARGET) $(TEST_ENV_TARGET) $(TEST_OUTPUT_TARGET) $(TEST_PROMPT_TARGET) $(TEST_CONVERSATION_TARGET) $(TEST_TOOLS_TARGET) $(TEST_SHELL_TARGET) $(TEST_FILE_TARGET) $(TEST_SMART_FILE_TARGET) $(TEST_RALPH_TARGET) $(TEST_TODO_MANAGER_TARGET) $(TEST_TODO_TOOL_TARGET) $(TEST_VECTOR_DB_TOOL_TARGET) $(TEST_TOKEN_MANAGER_TARGET) $(TEST_CONVERSATION_COMPACTOR_TARGET) $(TEST_INCOMPLETE_TASK_BUG_TARGET) $(TEST_MODEL_TOOLS_TARGET) $(TEST_MESSAGES_ARRAY_BUG_TARGET) $(TEST_VECTOR_DB_TARGET)

# Dependencies - remove duplicates (already defined above)
# CURL_VERSION and MBEDTLS_VERSION already defined at line 20-21
CURL_DIR = $(DEPDIR)/curl-$(CURL_VERSION)
MBEDTLS_DIR = $(DEPDIR)/mbedtls-$(MBEDTLS_VERSION)
HNSWLIB_DIR = $(DEPDIR)/hnswlib-$(HNSWLIB_VERSION)

# Dependency paths
CURL_LIB = $(CURL_DIR)/lib/.libs/libcurl.a
MBEDTLS_LIB1 = $(MBEDTLS_DIR)/library/libmbedtls.a
MBEDTLS_LIB2 = $(MBEDTLS_DIR)/library/libmbedx509.a  
MBEDTLS_LIB3 = $(MBEDTLS_DIR)/library/libmbedcrypto.a

# Include and library flags
INCLUDES = -I$(CURL_DIR)/include -I$(MBEDTLS_DIR)/include -I$(HNSWLIB_DIR) -I$(SRCDIR) -I$(SRCDIR)/core -I$(SRCDIR)/network -I$(SRCDIR)/llm -I$(SRCDIR)/session -I$(SRCDIR)/tools -I$(SRCDIR)/utils -I$(SRCDIR)/db
TEST_INCLUDES = $(INCLUDES) -I$(TESTDIR)/unity -I$(TESTDIR) -I$(TESTDIR)/core -I$(TESTDIR)/network -I$(TESTDIR)/llm -I$(TESTDIR)/session -I$(TESTDIR)/tools -I$(TESTDIR)/utils
LDFLAGS = -L$(CURL_DIR)/lib/.libs -L$(MBEDTLS_DIR)/library
LIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto
RALPH_TEST_LIBS = $(LIBS) -lpthread

# Bundled Links binary
LINKS_BUNDLED = build/links
EMBEDDED_LINKS_HEADER = $(SRCDIR)/embedded_links.h

# Default target
all: $(TARGET)

# Build bundled links test
$(TEST_BUNDLED_LINKS): test_bundled_links.c $(SRCDIR)/tools/tools_system.o $(SRCDIR)/tools/links_tool.o $(SRCDIR)/tools/shell_tool.o $(SRCDIR)/tools/file_tools.o $(EMBEDDED_LINKS_HEADER)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(SRCDIR)/tools/tools_system.o $(SRCDIR)/tools/links_tool.o $(SRCDIR)/tools/shell_tool.o $(SRCDIR)/tools/file_tools.o

# Build main executable
$(TARGET): $(EMBEDDED_LINKS_HEADER) $(OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS) -lpthread

# Build bin2c tool
$(BIN2C): build/bin2c.c
	$(CC) -O2 -o $@ $<

# Generate embedded links header
$(EMBEDDED_LINKS_HEADER): $(LINKS_BUNDLED) $(BIN2C)
	$(BIN2C) $(LINKS_BUNDLED) embedded_links > $(EMBEDDED_LINKS_HEADER)

# Download pre-built Cosmopolitan Links binary
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

# Compile source files (links_tool.o depends on embedded_links.h)
$(SRCDIR)/links_tool.o: $(SRCDIR)/links_tool.c $(EMBEDDED_LINKS_HEADER) $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile other source files
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile C++ source files
$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Test targets
test: $(ALL_TEST_TARGETS)
	./$(TEST_MAIN_TARGET)
	./$(TEST_HTTP_TARGET)
	./$(TEST_ENV_TARGET)
	./$(TEST_OUTPUT_TARGET)
	./$(TEST_PROMPT_TARGET)
	./$(TEST_CONVERSATION_TARGET)
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
	./$(TEST_VECTOR_DB_TARGET)

check: test

# Build test executables
$(TEST_MAIN_TARGET): $(TEST_MAIN_OBJECTS)
	$(CC) -o $@ $(TEST_MAIN_OBJECTS)

$(TEST_HTTP_TARGET): $(TEST_HTTP_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(LDFLAGS) -o $@ $(TEST_HTTP_OBJECTS) $(LIBS)

$(TEST_ENV_TARGET): $(TEST_ENV_OBJECTS)
	$(CC) -o $@ $(TEST_ENV_OBJECTS)

$(TEST_OUTPUT_TARGET): $(TEST_OUTPUT_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_OUTPUT_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_PROMPT_TARGET): $(TEST_PROMPT_OBJECTS)
	$(CC) -o $@ $(TEST_PROMPT_OBJECTS)

$(TEST_CONVERSATION_TARGET): $(TEST_CONVERSATION_OBJECTS)
	$(CC) -o $@ $(TEST_CONVERSATION_OBJECTS)

$(TEST_TOOLS_TARGET): $(TEST_TOOLS_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_TOOLS_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_SHELL_TARGET): $(TEST_SHELL_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_SHELL_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_FILE_TARGET): $(TEST_FILE_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_FILE_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_SMART_FILE_TARGET): $(TEST_SMART_FILE_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_SMART_FILE_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_RALPH_TARGET): $(TEST_RALPH_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_RALPH_OBJECTS) $(RALPH_TEST_LIBS)

$(TEST_TODO_MANAGER_TARGET): $(TEST_TODO_MANAGER_OBJECTS)
	$(CC) -o $@ $(TEST_TODO_MANAGER_OBJECTS)

$(TEST_TODO_TOOL_TARGET): $(TEST_TODO_TOOL_OBJECTS)
	$(CC) -o $@ $(TEST_TODO_TOOL_OBJECTS)

$(TEST_VECTOR_DB_TOOL_TARGET): $(TEST_VECTOR_DB_TOOL_OBJECTS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_VECTOR_DB_TOOL_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread -lm


$(TEST_TOKEN_MANAGER_TARGET): $(TEST_TOKEN_MANAGER_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) -o $@ $(TEST_TOKEN_MANAGER_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)

$(TEST_CONVERSATION_COMPACTOR_TARGET): $(TEST_CONVERSATION_COMPACTOR_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) -o $@ $(TEST_CONVERSATION_COMPACTOR_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)

$(TEST_INCOMPLETE_TASK_BUG_TARGET): $(TEST_INCOMPLETE_TASK_BUG_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_INCOMPLETE_TASK_BUG_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_MODEL_TOOLS_TARGET): $(TEST_MODEL_TOOLS_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_MODEL_TOOLS_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_MESSAGES_ARRAY_BUG_TARGET): $(TEST_MESSAGES_ARRAY_BUG_OBJECTS) $(EMBEDDED_LINKS_HEADER) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CXX) -o $@ $(TEST_MESSAGES_ARRAY_BUG_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) -lpthread

$(TEST_VECTOR_DB_TARGET): $(TEST_VECTOR_DB_OBJECTS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) -o $@ $(TEST_VECTOR_DB_OBJECTS) -lpthread -lm

# Compile test files
$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# Valgrind testing (excluding HTTP tests due to external library noise)
check-valgrind: $(ALL_TEST_TARGETS)
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MAIN_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_ENV_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_OUTPUT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PROMPT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_TARGET).aarch64.elf
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

# Valgrind testing for all tests (including external libraries - may show false positives)
check-valgrind-all: $(ALL_TEST_TARGETS)
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MAIN_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_HTTP_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_ENV_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_OUTPUT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PROMPT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TOOLS_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_SHELL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_FILE_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_RALPH_TARGET).aarch64.elf

# Dependencies (redundant - libraries are built automatically when needed)

# Create deps directory
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

# Clean targets
clean:
	rm -f $(OBJECTS) $(TEST_MAIN_OBJECTS) $(TEST_HTTP_OBJECTS) $(TEST_RALPH_OBJECTS) $(TARGET) $(ALL_TEST_TARGETS) $(TEST_BUNDLED_LINKS)
	rm -f src/*.o test/*.o test/unity/*.o
	rm -f *.aarch64.elf *.com.dbg *.dbg src/*.aarch64.elf src/*.com.dbg src/*.dbg test/*.aarch64.elf test/*.com.dbg test/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log
	rm -f $(EMBEDDED_LINKS_HEADER)
	# Clean non-tracked files from build directory (keep bin2c.c and links)
	find build -type f ! -name 'bin2c.c' ! -name 'links' -delete 2>/dev/null || true

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz
	rm -f $(LINKS_BUNDLED)

# This target works without any configuration whatsoever
realclean: distclean

.PHONY: all test check check-valgrind deps clean distclean realclean