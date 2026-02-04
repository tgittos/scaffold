# Source file definitions for ralph

# Core application sources
# Note: interrupt.c, common_utils.c, json_escape.c, debug_output.c, document_chunker.c
# have been migrated to lib/util/. See mk/lib.mk for LIB_UTIL_SOURCES.
CORE_SOURCES := \
    $(SRCDIR)/core/main.c \
    $(SRCDIR)/core/ralph.c \
    $(SRCDIR)/core/async_executor.c \
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
    $(SRCDIR)/utils/prompt_loader.c \
    $(SRCDIR)/utils/pdf_processor.c \
    $(SRCDIR)/utils/context_retriever.c \
    $(SRCDIR)/session/conversation_tracker.c \
    $(SRCDIR)/session/conversation_compactor.c \
    $(SRCDIR)/session/rolling_summary.c \
    $(SRCDIR)/session/session_manager.c \
    $(SRCDIR)/session/token_manager.c

# Policy module (approval gates, shell parsing, protected files)
POLICY_SOURCES := \
    $(SRCDIR)/policy/allowlist.c \
    $(SRCDIR)/policy/approval_gate.c \
    $(SRCDIR)/policy/approval_errors.c \
    $(SRCDIR)/policy/atomic_file.c \
    $(SRCDIR)/policy/gate_prompter.c \
    $(SRCDIR)/policy/path_normalize.c \
    $(SRCDIR)/policy/pattern_generator.c \
    $(SRCDIR)/policy/protected_files.c \
    $(SRCDIR)/policy/rate_limiter.c \
    $(SRCDIR)/policy/shell_parser.c \
    $(SRCDIR)/policy/shell_parser_cmd.c \
    $(SRCDIR)/policy/shell_parser_ps.c \
    $(SRCDIR)/policy/subagent_approval.c \
    $(SRCDIR)/policy/tool_args.c \
    $(SRCDIR)/policy/verified_file_context.c \
    $(SRCDIR)/policy/verified_file_python.c

# Tool system
# Note: Core tool infrastructure and built-in tools have been migrated to lib/tools/
# See mk/lib.mk for LIB_TOOLS_SOURCES. Only Python tools remain in src/tools/.
TOOL_SOURCES := \
    $(SRCDIR)/tools/python_tool.c \
    $(SRCDIR)/tools/python_tool_files.c

# MCP system
MCP_SOURCES := \
    $(SRCDIR)/mcp/mcp_client.c \
    $(SRCDIR)/mcp/mcp_transport.c \
    $(SRCDIR)/mcp/mcp_transport_stdio.c \
    $(SRCDIR)/mcp/mcp_transport_http.c

# Messaging system
MESSAGING_SOURCES := \
    $(SRCDIR)/messaging/notification_formatter.c

# LLM providers - migrated to lib/llm/providers/
# Note: All LLM providers and embeddings have been migrated to lib/llm/.
# See mk/lib.mk for LIB_LLM_SOURCES.
PROVIDER_SOURCES :=

# Model implementations - migrated to lib/llm/models/. See mk/lib.mk for LIB_LLM_SOURCES.
MODEL_SOURCES :=

# Database - migrated to lib/db/. See mk/lib.mk for LIB_DB_SOURCES and LIB_DB_CPP_SOURCES.
# PDF - migrated to lib/pdf/. See mk/lib.mk for LIB_PDF_SOURCES.

# Utilities
UTILS_EXTRA_SOURCES := \
    $(SRCDIR)/utils/ralph_home.c

# Combined sources (database and PDF are now in lib/, see mk/lib.mk)
C_SOURCES := $(CORE_SOURCES) $(POLICY_SOURCES) $(TOOL_SOURCES) $(MCP_SOURCES) $(MESSAGING_SOURCES) \
    $(PROVIDER_SOURCES) $(MODEL_SOURCES) $(UTILS_EXTRA_SOURCES)
CPP_SOURCES :=
SOURCES := $(C_SOURCES) $(CPP_SOURCES)
OBJECTS := $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)
HEADERS := $(wildcard $(SRCDIR)/*/*.h)

# Reusable dependency groups for tests
NETWORK_DEPS := \
    $(SRCDIR)/network/http_client.c \
    $(SRCDIR)/network/embedded_cacert.c \
    $(SRCDIR)/network/api_error.c \
    $(LIBDIR)/util/interrupt.c

EMBEDDING_DEPS := \
    $(LIBDIR)/llm/embeddings.c \
    $(LIBDIR)/llm/embeddings_service.c \
    $(LIBDIR)/llm/embedding_provider.c \
    $(LIBDIR)/llm/providers/openai_embedding_provider.c \
    $(LIBDIR)/llm/providers/local_embedding_provider.c

UTIL_DEPS := \
    $(LIBDIR)/util/json_escape.c \
    $(LIBDIR)/ui/output_formatter.c \
    $(LIBDIR)/ui/json_output.c \
    $(LIBDIR)/util/debug_output.c \
    $(LIBDIR)/util/common_utils.c \
    $(LIBDIR)/util/document_chunker.c \
    $(SRCDIR)/utils/pdf_processor.c \
    $(SRCDIR)/utils/context_retriever.c \
    $(SRCDIR)/utils/config.c \
    $(LIBDIR)/util/uuid_utils.c \
    $(SRCDIR)/utils/ralph_home.c \
    $(LIBDIR)/ui/terminal.c \
    $(LIBDIR)/ui/spinner.c

RALPH_CORE_DEPS := \
    $(SRCDIR)/core/ralph.c \
    $(SRCDIR)/core/async_executor.c \
    $(SRCDIR)/core/tool_executor.c \
    $(SRCDIR)/core/streaming_handler.c \
    $(SRCDIR)/core/context_enhancement.c \
    $(SRCDIR)/core/recap.c \
    $(LIBDIR)/ipc/pipe_notifier.c \
    $(SRCDIR)/policy/allowlist.c \
    $(SRCDIR)/policy/approval_gate.c \
    $(SRCDIR)/policy/approval_errors.c \
    $(SRCDIR)/policy/subagent_approval.c \
    $(SRCDIR)/policy/atomic_file.c \
    $(SRCDIR)/policy/gate_prompter.c \
    $(SRCDIR)/policy/path_normalize.c \
    $(SRCDIR)/policy/pattern_generator.c \
    $(SRCDIR)/policy/protected_files.c \
    $(SRCDIR)/policy/rate_limiter.c \
    $(SRCDIR)/policy/shell_parser.c \
    $(SRCDIR)/policy/shell_parser_cmd.c \
    $(SRCDIR)/policy/shell_parser_ps.c \
    $(SRCDIR)/policy/tool_args.c \
    $(SRCDIR)/policy/verified_file_context.c \
    $(SRCDIR)/policy/verified_file_python.c \
    $(SRCDIR)/utils/prompt_loader.c \
    $(SRCDIR)/session/conversation_tracker.c \
    $(SRCDIR)/session/conversation_compactor.c \
    $(SRCDIR)/session/rolling_summary.c \
    $(SRCDIR)/session/session_manager.c \
    $(SRCDIR)/network/api_common.c \
    $(SRCDIR)/network/streaming.c \
    $(SRCDIR)/session/token_manager.c \
    $(LIBDIR)/llm/llm_provider.c \
    $(LIBDIR)/llm/providers/openai_provider.c \
    $(LIBDIR)/llm/providers/anthropic_provider.c \
    $(LIBDIR)/llm/providers/local_ai_provider.c \
    $(SRCDIR)/mcp/mcp_client.c \
    $(SRCDIR)/mcp/mcp_transport.c \
    $(SRCDIR)/mcp/mcp_transport_stdio.c \
    $(SRCDIR)/mcp/mcp_transport_http.c \
    $(LIBDIR)/ipc/message_poller.c \
    $(MESSAGING_SOURCES)

# Verified file I/O dependencies (needed by Python tools for TOCTOU-safe access)
# Full set including atomic_file.c and path_normalize.c
VERIFIED_FILE_DEPS := \
    $(SRCDIR)/policy/verified_file_context.c \
    $(SRCDIR)/policy/verified_file_python.c \
    $(SRCDIR)/policy/atomic_file.c \
    $(SRCDIR)/policy/path_normalize.c

# COMPLEX_DEPS only includes verified_file_context.c and verified_file_python.c,
# NOT atomic_file.c and path_normalize.c which are already in RALPH_CORE_DEPS.
# Tests that use COMPLEX_DEPS without RALPH_CORE_DEPS and need the full verified
# file deps should include VERIFIED_FILE_DEPS directly.
# Note: LIB_TOOLS_SOURCES defined in lib.mk must be included for tool system core.
COMPLEX_DEPS := \
    $(TOOL_SOURCES) \
    $(LIB_TOOLS_SOURCES) \
    $(LIB_LLM_SOURCES) \
    $(MODEL_SOURCES) \
    $(UTIL_DEPS) \
    $(LIB_DB_SOURCES) \
    $(LIBDIR)/ipc/message_store.c \
    $(EMBEDDING_DEPS) \
    $(LIBDIR)/pdf/pdf_extractor.c \
    $(NETWORK_DEPS) \
    $(SRCDIR)/policy/verified_file_context.c \
    $(SRCDIR)/policy/verified_file_python.c \
    $(SRCDIR)/policy/atomic_file.c \
    $(SRCDIR)/policy/path_normalize.c

CONV_DEPS := \
    $(SRCDIR)/session/conversation_tracker.c \
    $(LIB_DB_SOURCES) \
    $(EMBEDDING_DEPS) \
    $(NETWORK_DEPS) \
    $(LIBDIR)/util/json_escape.c \
    $(SRCDIR)/utils/config.c \
    $(LIBDIR)/util/debug_output.c \
    $(LIBDIR)/util/common_utils.c \
    $(SRCDIR)/utils/ralph_home.c
