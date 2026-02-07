# Library build configuration for libralph

# Library output
LIBRALPH := $(BUILDDIR)/libralph.a

# =============================================================================
# LIBRARY SOURCES
# =============================================================================

# IPC module
LIB_IPC_SOURCES := \
    $(LIBDIR)/ipc/pipe_notifier.c \
    $(LIBDIR)/ipc/agent_identity.c \
    $(LIBDIR)/ipc/message_store.c \
    $(LIBDIR)/ipc/message_poller.c \
    $(LIBDIR)/ipc/notification_formatter.c

# UI module
LIB_UI_SOURCES := \
    $(LIBDIR)/ui/terminal.c \
    $(LIBDIR)/ui/spinner.c \
    $(LIBDIR)/ui/output_formatter.c \
    $(LIBDIR)/ui/json_output.c \
    $(LIBDIR)/ui/memory_commands.c

# Tools module
LIB_TOOLS_SOURCES := \
    $(LIBDIR)/tools/tools_system.c \
    $(LIBDIR)/tools/tool_format_openai.c \
    $(LIBDIR)/tools/tool_format_anthropic.c \
    $(LIBDIR)/tools/tool_param_dsl.c \
    $(LIBDIR)/tools/tool_result_builder.c \
    $(LIBDIR)/tools/tool_extension.c \
    $(LIBDIR)/tools/builtin_tools.c \
    $(LIBDIR)/tools/memory_tool.c \
    $(LIBDIR)/tools/messaging_tool.c \
    $(LIBDIR)/tools/pdf_tool.c \
    $(LIBDIR)/tools/todo_manager.c \
    $(LIBDIR)/tools/todo_tool.c \
    $(LIBDIR)/tools/todo_display.c \
    $(LIBDIR)/tools/vector_db_tool.c \
    $(LIBDIR)/tools/subagent_tool.c \
    $(LIBDIR)/tools/subagent_process.c

# Agent module
LIB_AGENT_SOURCES := \
    $(LIBDIR)/agent/agent.c \
    $(LIBDIR)/agent/session.c \
    $(LIBDIR)/agent/session_configurator.c \
    $(LIBDIR)/agent/message_dispatcher.c \
    $(LIBDIR)/agent/message_processor.c \
    $(LIBDIR)/agent/async_executor.c \
    $(LIBDIR)/agent/tool_executor.c \
    $(LIBDIR)/agent/tool_batch_executor.c \
    $(LIBDIR)/agent/api_round_trip.c \
    $(LIBDIR)/agent/conversation_state.c \
    $(LIBDIR)/agent/iterative_loop.c \
    $(LIBDIR)/agent/tool_orchestration.c \
    $(LIBDIR)/agent/streaming_handler.c \
    $(LIBDIR)/agent/context_enhancement.c \
    $(LIBDIR)/agent/recap.c \
    $(LIBDIR)/agent/repl.c

# LLM module
LIB_LLM_SOURCES := \
    $(LIBDIR)/llm/llm_provider.c \
    $(LIBDIR)/llm/llm_client.c \
    $(LIBDIR)/llm/model_capabilities.c \
    $(LIBDIR)/llm/embeddings.c \
    $(LIBDIR)/llm/embedding_provider.c \
    $(LIBDIR)/llm/embeddings_service.c \
    $(LIBDIR)/llm/providers/openai_provider.c \
    $(LIBDIR)/llm/providers/anthropic_provider.c \
    $(LIBDIR)/llm/providers/local_ai_provider.c \
    $(LIBDIR)/llm/providers/openai_embedding_provider.c \
    $(LIBDIR)/llm/providers/local_embedding_provider.c \
    $(LIBDIR)/llm/models/response_processing.c \
    $(LIBDIR)/llm/models/claude_model.c \
    $(LIBDIR)/llm/models/gpt_model.c \
    $(LIBDIR)/llm/models/qwen_model.c \
    $(LIBDIR)/llm/models/deepseek_model.c \
    $(LIBDIR)/llm/models/default_model.c

# Services module
LIB_SERVICES_SOURCES := $(LIBDIR)/services/services.c

# Session module
LIB_SESSION_SOURCES := \
    $(LIBDIR)/session/token_manager.c \
    $(LIBDIR)/session/conversation_tracker.c \
    $(LIBDIR)/session/conversation_compactor.c \
    $(LIBDIR)/session/rolling_summary.c \
    $(LIBDIR)/session/session_manager.c

# Policy module (approval gates, shell parsing, file protection)
LIB_POLICY_SOURCES := \
    $(LIBDIR)/policy/rate_limiter.c \
    $(LIBDIR)/policy/allowlist.c \
    $(LIBDIR)/policy/shell_parser.c \
    $(LIBDIR)/policy/shell_parser_cmd.c \
    $(LIBDIR)/policy/shell_parser_ps.c \
    $(LIBDIR)/policy/path_normalize.c \
    $(LIBDIR)/policy/protected_files.c \
    $(LIBDIR)/policy/atomic_file.c \
    $(LIBDIR)/policy/verified_file_context.c \
    $(LIBDIR)/policy/verified_file_python.c \
    $(LIBDIR)/policy/tool_args.c \
    $(LIBDIR)/policy/pattern_generator.c \
    $(LIBDIR)/policy/gate_prompter.c \
    $(LIBDIR)/policy/approval_gate.c \
    $(LIBDIR)/policy/approval_errors.c \
    $(LIBDIR)/policy/subagent_approval.c

# Util module (generic utilities)
LIB_UTIL_SOURCES := \
    $(LIBDIR)/util/uuid_utils.c \
    $(LIBDIR)/util/common_utils.c \
    $(LIBDIR)/util/json_escape.c \
    $(LIBDIR)/util/debug_output.c \
    $(LIBDIR)/util/document_chunker.c \
    $(LIBDIR)/util/interrupt.c \
    $(LIBDIR)/util/config.c \
    $(LIBDIR)/util/prompt_loader.c \
    $(LIBDIR)/util/ralph_home.c \
    $(LIBDIR)/util/context_retriever.c

# PDF module
LIB_PDF_SOURCES := $(LIBDIR)/pdf/pdf_extractor.c

# Database module
LIB_DB_SOURCES := \
    $(LIBDIR)/db/sqlite_dal.c \
    $(LIBDIR)/db/vector_db.c \
    $(LIBDIR)/db/vector_db_service.c \
    $(LIBDIR)/db/metadata_store.c \
    $(LIBDIR)/db/document_store.c \
    $(LIBDIR)/db/task_store.c

# Database C++ sources
LIB_DB_CPP_SOURCES := $(LIBDIR)/db/hnswlib_wrapper.cpp

# Workflow module
LIB_WORKFLOW_SOURCES := $(LIBDIR)/workflow/workflow.c

# Network module
LIB_NETWORK_SOURCES := \
    $(LIBDIR)/network/http_client.c \
    $(LIBDIR)/network/api_common.c \
    $(LIBDIR)/network/api_error.c \
    $(LIBDIR)/network/streaming.c \
    $(LIBDIR)/network/embedded_cacert.c

# MCP module (Model Context Protocol)
LIB_MCP_SOURCES := \
    $(LIBDIR)/mcp/mcp_client.c \
    $(LIBDIR)/mcp/mcp_transport.c \
    $(LIBDIR)/mcp/mcp_transport_stdio.c \
    $(LIBDIR)/mcp/mcp_transport_http.c

# Combined library sources
LIB_C_SOURCES := $(LIB_IPC_SOURCES) $(LIB_UI_SOURCES) $(LIB_TOOLS_SOURCES) \
    $(LIB_AGENT_SOURCES) $(LIB_SERVICES_SOURCES) $(LIB_LLM_SOURCES) \
    $(LIB_SESSION_SOURCES) $(LIB_POLICY_SOURCES) $(LIB_DB_SOURCES) \
    $(LIB_WORKFLOW_SOURCES) $(LIB_UTIL_SOURCES) $(LIB_PDF_SOURCES) \
    $(LIB_NETWORK_SOURCES) $(LIB_MCP_SOURCES)

LIB_CPP_SOURCES := $(LIB_DB_CPP_SOURCES)

LIB_SOURCES := $(LIB_C_SOURCES) $(LIB_CPP_SOURCES)
LIB_OBJECTS := $(LIB_C_SOURCES:.c=.o) $(LIB_CPP_SOURCES:.cpp=.o)
# =============================================================================
# LIBRARY BUILD RULES
# =============================================================================

# Library include path (for src/ to use lib/)
LIB_INCLUDES := -I$(LIBDIR)

# Compile library C files
$(LIBDIR)/%.o: $(LIBDIR)/%.c | $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIB_INCLUDES) -c $< -o $@

# Compile library C++ files
$(LIBDIR)/%.o: $(LIBDIR)/%.cpp | $(COMPILE_DEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIB_INCLUDES) -c $< -o $@

# Build the library archive
$(LIBRALPH): $(LIB_OBJECTS)
	@mkdir -p $(BUILDDIR)
	@if [ -n "$(LIB_OBJECTS)" ]; then \
		$(AR) rcs $@ $(LIB_OBJECTS); \
	else \
		touch $@; \
	fi

# =============================================================================
# LIBRARY TARGETS
# =============================================================================

.PHONY: libralph libralph-clean

libralph: $(LIBRALPH)

libralph-clean:
	rm -f $(LIB_OBJECTS) $(LIBRALPH)
	find $(LIBDIR) -name "*.o" -delete 2>/dev/null || true
	find $(LIBDIR) -name "*.o.d" -delete 2>/dev/null || true

# =============================================================================
# INTEGRATION WITH MAIN BUILD
# =============================================================================

# Update INCLUDES to include lib/ for all compilation
INCLUDES += $(LIB_INCLUDES)
TEST_INCLUDES += $(LIB_INCLUDES)

# Clean target should also clean library
clean: libralph-clean
