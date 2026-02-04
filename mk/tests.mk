# Test definitions and rules for ralph
# Uses explicit templates to reduce repetition while maintaining clarity

UNITY := $(TESTDIR)/unity/unity.c

# =============================================================================
# TEST DEFINITION TEMPLATES
# =============================================================================

# Template for test variable generation
# $(1) = test name (lowercase, no prefix)
# $(2) = test file path (relative to TESTDIR, no .c)
# $(3) = additional source files
define def_test
TEST_$(1)_SOURCES := $$(TESTDIR)/$(2).c $(3) $$(UNITY)
TEST_$(1)_OBJECTS := $$(TEST_$(1)_SOURCES:.c=.o)
TEST_$(1)_TARGET := $$(TESTDIR)/test_$(1)
ALL_TEST_TARGETS += $$(TEST_$(1)_TARGET)
endef

# Template for mixed C/C++ test (adds CPP objects)
# Uses $(sort) to deduplicate sources when tests use multiple dependency sets
define def_test_mixed
TEST_$(1)_SOURCES := $$(sort $$(TESTDIR)/$(2).c $(3) $$(UNITY))
TEST_$(1)_OBJECTS := $$(TEST_$(1)_SOURCES:.c=.o) $$(LIB_DB_CPP_SOURCES:.cpp=.o)
TEST_$(1)_TARGET := $$(TESTDIR)/test_$(1)
ALL_TEST_TARGETS += $$(TEST_$(1)_TARGET)
endef

# =============================================================================
# MINIMAL TESTS (no external libraries)
# =============================================================================

$(eval $(call def_test,main,core/test_main,))
$(eval $(call def_test,cli_flags,core/test_cli_flags,))
$(eval $(call def_test,interrupt,core/test_interrupt,$(LIBDIR)/util/interrupt.c))
$(eval $(call def_test,async_executor,core/test_async_executor,$(SRCDIR)/core/async_executor.c $(LIBDIR)/util/interrupt.c $(LIBDIR)/util/debug_output.c $(LIBDIR)/ipc/pipe_notifier.c $(TESTDIR)/stubs/ralph_stub.c))
$(eval $(call def_test,pipe_notifier,utils/test_pipe_notifier,$(LIBDIR)/ipc/pipe_notifier.c))
$(eval $(call def_test,agent_identity,core/test_agent_identity,$(LIBDIR)/ipc/agent_identity.c))
$(eval $(call def_test,prompt,utils/test_prompt_loader,$(SRCDIR)/utils/prompt_loader.c $(SRCDIR)/utils/ralph_home.c))
$(eval $(call def_test,ralph_home,utils/test_ralph_home,$(SRCDIR)/utils/ralph_home.c))
$(eval $(call def_test,todo_manager,tools/test_todo_manager,$(LIBDIR)/tools/todo_manager.c))
$(eval $(call def_test_mixed,tool_param_dsl,tools/test_tool_param_dsl,$(COMPLEX_DEPS)))
$(eval $(call def_test,document_chunker,test_document_chunker,$(LIBDIR)/util/document_chunker.c $(LIBDIR)/util/common_utils.c))
$(eval $(call def_test,streaming,network/test_streaming,$(LIBDIR)/network/streaming.c))
$(eval $(call def_test,darray,test_darray,))
$(eval $(call def_test,ptrarray,test_ptrarray,))
$(eval $(call def_test,rate_limiter,policy/test_rate_limiter,$(LIBDIR)/policy/rate_limiter.c))
$(eval $(call def_test,allowlist,policy/test_allowlist,$(LIBDIR)/policy/allowlist.c $(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_cmd.c $(LIBDIR)/policy/shell_parser_ps.c))
$(eval $(call def_test,tool_args,test_tool_args,$(LIBDIR)/policy/tool_args.c))
$(eval $(call def_test,gate_prompter,policy/test_gate_prompter,$(LIBDIR)/policy/gate_prompter.c))

# Gate dependencies (used by multiple gate-related tests)
GATE_DEPS := \
    $(LIBDIR)/policy/allowlist.c \
    $(LIBDIR)/policy/approval_gate.c \
    $(LIBDIR)/policy/approval_errors.c \
    $(LIBDIR)/policy/atomic_file.c \
    $(LIBDIR)/policy/gate_prompter.c \
    $(LIBDIR)/policy/pattern_generator.c \
    $(LIBDIR)/policy/rate_limiter.c \
    $(LIBDIR)/policy/shell_parser.c \
    $(LIBDIR)/policy/shell_parser_cmd.c \
    $(LIBDIR)/policy/shell_parser_ps.c \
    $(LIBDIR)/policy/subagent_approval.c \
    $(LIBDIR)/policy/tool_args.c \
    $(LIBDIR)/util/debug_output.c \
    $(SRCDIR)/utils/ralph_home.c \
    $(LIBDIR)/util/json_escape.c \
    $(TESTDIR)/stubs/subagent_stub.c \
    $(TESTDIR)/stubs/output_formatter_stub.c

$(eval $(call def_test,approval_gate,policy/test_approval_gate,$(GATE_DEPS)))
$(eval $(call def_test,atomic_file,policy/test_atomic_file,$(LIBDIR)/policy/atomic_file.c))
$(eval $(call def_test,path_normalize,policy/test_path_normalize,$(LIBDIR)/policy/path_normalize.c))
$(eval $(call def_test,verified_file_context,test_verified_file_context,$(LIBDIR)/policy/verified_file_context.c $(LIBDIR)/policy/atomic_file.c $(LIBDIR)/policy/path_normalize.c))
$(eval $(call def_test,protected_files,policy/test_protected_files,$(LIBDIR)/policy/protected_files.c $(LIBDIR)/policy/path_normalize.c))
$(eval $(call def_test,shell_parser,policy/test_shell_parser,$(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_cmd.c $(LIBDIR)/policy/shell_parser_ps.c))
$(eval $(call def_test,shell_parser_cmd,policy/test_shell_parser_cmd,$(LIBDIR)/policy/shell_parser_cmd.c $(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_ps.c))
$(eval $(call def_test,shell_parser_ps,policy/test_shell_parser_ps,$(LIBDIR)/policy/shell_parser_ps.c $(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_cmd.c))
$(eval $(call def_test,subagent_approval,policy/test_subagent_approval,$(GATE_DEPS)))
$(eval $(call def_test,approval_gate_integration,policy/test_approval_gate_integration,$(GATE_DEPS) $(TESTDIR)/stubs/python_tool_stub.c))

$(TEST_main_TARGET): $(TEST_main_OBJECTS)
	$(CC) -o $@ $^

$(TEST_cli_flags_TARGET): $(TEST_cli_flags_OBJECTS) ralph
	$(CC) -o $@ $(TEST_cli_flags_OBJECTS)

$(TEST_interrupt_TARGET): $(TEST_interrupt_OBJECTS)
	$(CC) -o $@ $^

$(TEST_async_executor_TARGET): $(TEST_async_executor_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_async_executor_OBJECTS) $(CJSON_LIB) -lpthread

$(TEST_pipe_notifier_TARGET): $(TEST_pipe_notifier_OBJECTS)
	$(CC) -o $@ $^

$(TEST_agent_identity_TARGET): $(TEST_agent_identity_OBJECTS)
	$(CC) -o $@ $^ -lpthread

$(TEST_prompt_TARGET): $(TEST_prompt_OBJECTS)
	$(CC) -o $@ $^

$(TEST_ralph_home_TARGET): $(TEST_ralph_home_OBJECTS)
	$(CC) -o $@ $^

$(TEST_todo_manager_TARGET): $(TEST_todo_manager_OBJECTS)
	$(CC) -o $@ $^

$(TEST_tool_param_dsl_TARGET): $(TEST_tool_param_dsl_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_tool_param_dsl_OBJECTS) $(LIBS_STANDARD)

$(TEST_document_chunker_TARGET): $(TEST_document_chunker_OBJECTS)
	$(CC) -o $@ $^

$(TEST_streaming_TARGET): $(TEST_streaming_OBJECTS)
	$(CC) -o $@ $^

$(TEST_darray_TARGET): $(TEST_darray_OBJECTS)
	$(CC) -o $@ $^

$(TEST_ptrarray_TARGET): $(TEST_ptrarray_OBJECTS)
	$(CC) -o $@ $^

$(TEST_rate_limiter_TARGET): $(TEST_rate_limiter_OBJECTS)
	$(CC) -o $@ $^

$(TEST_allowlist_TARGET): $(TEST_allowlist_OBJECTS)
	$(CC) -o $@ $^

$(TEST_tool_args_TARGET): $(TEST_tool_args_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_tool_args_OBJECTS) $(CJSON_LIB)

$(TEST_gate_prompter_TARGET): $(TEST_gate_prompter_OBJECTS)
	$(CC) -o $@ $^

$(TEST_approval_gate_TARGET): $(TEST_approval_gate_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_approval_gate_OBJECTS) $(CJSON_LIB)

$(TEST_atomic_file_TARGET): $(TEST_atomic_file_OBJECTS)
	$(CC) -o $@ $^

$(TEST_path_normalize_TARGET): $(TEST_path_normalize_OBJECTS)
	$(CC) -o $@ $^

$(TEST_protected_files_TARGET): $(TEST_protected_files_OBJECTS)
	$(CC) -o $@ $^

$(TEST_verified_file_context_TARGET): $(TEST_verified_file_context_OBJECTS)
	$(CC) -o $@ $^

$(TEST_shell_parser_TARGET): $(TEST_shell_parser_OBJECTS)
	$(CC) -o $@ $^

$(TEST_shell_parser_cmd_TARGET): $(TEST_shell_parser_cmd_OBJECTS)
	$(CC) -o $@ $^

$(TEST_shell_parser_ps_TARGET): $(TEST_shell_parser_ps_OBJECTS)
	$(CC) -o $@ $^

$(TEST_subagent_approval_TARGET): $(TEST_subagent_approval_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_subagent_approval_OBJECTS) $(CJSON_LIB)

$(TEST_approval_gate_integration_TARGET): $(TEST_approval_gate_integration_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_approval_gate_integration_OBJECTS) $(CJSON_LIB)

# =============================================================================
# CJSON TESTS
# =============================================================================

$(eval $(call def_test,config,utils/test_config,$(SRCDIR)/utils/config.c $(SRCDIR)/utils/ralph_home.c))
$(eval $(call def_test,debug_output,utils/test_debug_output,$(LIBDIR)/util/debug_output.c))
$(eval $(call def_test,spinner,utils/test_spinner,$(LIBDIR)/ui/terminal.c $(LIBDIR)/ui/spinner.c $(TESTDIR)/stubs/output_formatter_stub.c))
$(eval $(call def_test,terminal,utils/test_terminal,$(LIBDIR)/ui/terminal.c $(TESTDIR)/stubs/output_formatter_stub.c))

$(TEST_config_TARGET): $(TEST_config_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_config_OBJECTS) $(CJSON_LIB)

$(TEST_debug_output_TARGET): $(TEST_debug_output_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_debug_output_OBJECTS) $(CJSON_LIB)

$(TEST_spinner_TARGET): $(TEST_spinner_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_spinner_OBJECTS) $(CJSON_LIB) -lpthread

$(TEST_terminal_TARGET): $(TEST_terminal_OBJECTS)
	$(CC) -o $@ $^

# =============================================================================
# PDF TEST
# =============================================================================

$(eval $(call def_test,pdf_extractor,pdf/test_pdf_extractor,$(LIBDIR)/pdf/pdf_extractor.c))

$(TEST_pdf_extractor_TARGET): $(TEST_pdf_extractor_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB)
	$(CC) -o $@ $(TEST_pdf_extractor_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB) -lm

# =============================================================================
# HTTP RETRY TEST
# =============================================================================

$(eval $(call def_test,http_retry,network/test_http_retry,$(LIBDIR)/network/api_error.c $(SRCDIR)/utils/config.c $(SRCDIR)/utils/ralph_home.c))

$(TEST_http_retry_TARGET): $(TEST_http_retry_OBJECTS) $(CJSON_LIB) $(LIBS_MBEDTLS)
	$(CC) -o $@ $(TEST_http_retry_OBJECTS) $(CJSON_LIB) $(LIBS_MBEDTLS) -lm

# =============================================================================
# SQLITE TESTS
# =============================================================================

$(eval $(call def_test,sqlite_dal,db/test_sqlite_dal,$(LIBDIR)/db/sqlite_dal.c $(SRCDIR)/utils/ralph_home.c))
$(eval $(call def_test,task_store,db/test_task_store,$(LIBDIR)/db/task_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/uuid_utils.c $(SRCDIR)/utils/ralph_home.c))
$(eval $(call def_test,message_store,db/test_message_store,$(LIBDIR)/ipc/message_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/uuid_utils.c $(SRCDIR)/utils/ralph_home.c))

$(TEST_sqlite_dal_TARGET): $(TEST_sqlite_dal_OBJECTS) $(SQLITE_LIB)
	$(CC) -o $@ $(TEST_sqlite_dal_OBJECTS) $(SQLITE_LIB) -lpthread -lm

$(TEST_task_store_TARGET): $(TEST_task_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB)
	$(CC) -o $@ $(TEST_task_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lpthread -lm

$(TEST_message_store_TARGET): $(TEST_message_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB)
	$(CC) -o $@ $(TEST_message_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lpthread -lm

# Messaging deps
MESSAGING_DEPS := \
    $(LIBDIR)/ipc/message_poller.c \
    $(SRCDIR)/messaging/notification_formatter.c \
    $(LIBDIR)/ipc/message_store.c \
    $(LIBDIR)/db/sqlite_dal.c \
    $(LIBDIR)/util/uuid_utils.c \
    $(SRCDIR)/utils/ralph_home.c \
    $(LIBDIR)/ipc/pipe_notifier.c \
    $(LIBDIR)/ui/terminal.c \
    $(TESTDIR)/stubs/output_formatter_stub.c

$(eval $(call def_test,message_poller,messaging/test_message_poller,$(MESSAGING_DEPS)))
$(eval $(call def_test,notification_formatter,messaging/test_notification_formatter,$(MESSAGING_DEPS)))

$(TEST_message_poller_TARGET): $(TEST_message_poller_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_message_poller_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB) -lpthread -lm

$(TEST_notification_formatter_TARGET): $(TEST_notification_formatter_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_notification_formatter_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB) -lpthread -lm

# =============================================================================
# CONVERSATION TESTS (special linking)
# =============================================================================

# Extra objects needed by conversation test (not in test sources)
CONV_EXTRA_OBJECTS := $(LIB_DB_SOURCES:.c=.o) $(LIB_DB_CPP_SOURCES:.cpp=.o) \
    $(LIBDIR)/llm/embeddings.o $(LIBDIR)/llm/embeddings_service.o $(LIBDIR)/llm/embedding_provider.o \
    $(LIBDIR)/llm/providers/openai_embedding_provider.o $(LIBDIR)/llm/providers/local_embedding_provider.o \
    $(LIBDIR)/network/http_client.o $(LIBDIR)/network/embedded_cacert.o $(LIBDIR)/network/api_error.o \
    $(LIBDIR)/util/interrupt.o \
    $(SRCDIR)/utils/config.o $(LIBDIR)/util/debug_output.o $(LIBDIR)/util/common_utils.o \
    $(SRCDIR)/utils/ralph_home.o

$(eval $(call def_test,conversation,session/test_conversation_tracker,$(LIBDIR)/session/conversation_tracker.c $(LIBDIR)/util/json_escape.c))
$(eval $(call def_test,conversation_vdb,session/test_conversation_vector_db,$(LIBDIR)/session/conversation_tracker.c $(LIBDIR)/util/json_escape.c $(TESTDIR)/mock_api_server.c $(TESTDIR)/mock_embeddings.c $(TESTDIR)/mock_embeddings_server.c))
$(eval $(call def_test_mixed,tool_calls_not_stored,session/test_tool_calls_not_stored,$(CONV_DEPS)))

$(TEST_conversation_TARGET): $(TEST_conversation_OBJECTS) $(CONV_EXTRA_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_conversation_OBJECTS) $(CONV_EXTRA_OBJECTS) \
		$(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_conversation_vdb_TARGET): $(TEST_conversation_vdb_OBJECTS) $(CONV_EXTRA_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_conversation_vdb_OBJECTS) $(CONV_EXTRA_OBJECTS) $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

$(TEST_tool_calls_not_stored_TARGET): $(TEST_tool_calls_not_stored_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_tool_calls_not_stored_OBJECTS) $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) -lm -lpthread

# =============================================================================
# STANDARD MIXED TESTS (CXX linker with LIBS_STANDARD)
# =============================================================================

$(eval $(call def_test_mixed,json_output,utils/test_json_output,$(LIBDIR)/network/streaming.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,output,utils/test_output_formatter,$(COMPLEX_DEPS)))
# test_tools_system includes approval gate integration tests, requires RALPH_CORE_DEPS
$(eval $(call def_test_mixed,tools_system,tools/test_tools_system,$(RALPH_CORE_DEPS) $(COMPLEX_DEPS)))
# Mock embeddings sources for tests that need mocked embedding API
MOCK_EMBEDDINGS_SOURCES := $(TESTDIR)/mock_api_server.c $(TESTDIR)/mock_embeddings.c $(TESTDIR)/mock_embeddings_server.c

$(eval $(call def_test_mixed,vector_db_tool,tools/test_vector_db_tool,$(MOCK_EMBEDDINGS_SOURCES) $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,memory_tool,tools/test_memory_tool,$(MOCK_EMBEDDINGS_SOURCES) $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,memory_mgmt,test_memory_management,$(LIBDIR)/ui/memory_commands.c $(LIBDIR)/ui/terminal.c $(LIB_DB_SOURCES) $(EMBEDDING_DEPS) $(SRCDIR)/utils/config.c $(NETWORK_DEPS) $(LIBDIR)/util/common_utils.c $(LIBDIR)/util/debug_output.c $(SRCDIR)/utils/ralph_home.c))
$(eval $(call def_test_mixed,token_manager,session/test_token_manager,$(LIBDIR)/session/token_manager.c $(LIBDIR)/session/rolling_summary.c $(LIBDIR)/session/session_manager.c $(LIBDIR)/session/conversation_tracker.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,conversation_compactor,session/test_conversation_compactor,$(LIBDIR)/session/conversation_compactor.c $(LIBDIR)/session/rolling_summary.c $(LIBDIR)/session/session_manager.c $(LIBDIR)/session/conversation_tracker.c $(LIBDIR)/session/token_manager.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,rolling_summary,session/test_rolling_summary,$(LIBDIR)/session/rolling_summary.c $(LIBDIR)/session/session_manager.c $(LIBDIR)/session/conversation_tracker.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,model_tools,llm/test_model_tools,$(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,openai_streaming,llm/test_openai_streaming,$(LIBDIR)/network/streaming.c $(LIBDIR)/llm/providers/openai_provider.c $(LIBDIR)/llm/providers/anthropic_provider.c $(LIBDIR)/llm/providers/local_ai_provider.c $(LIBDIR)/network/api_common.c $(LIBDIR)/session/conversation_tracker.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,anthropic_streaming,llm/test_anthropic_streaming,$(LIBDIR)/network/streaming.c $(LIBDIR)/llm/providers/openai_provider.c $(LIBDIR)/llm/providers/anthropic_provider.c $(LIBDIR)/llm/providers/local_ai_provider.c $(LIBDIR)/network/api_common.c $(LIBDIR)/session/conversation_tracker.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,messages_array_bug,network/test_messages_array_bug,$(LIBDIR)/network/api_common.c $(LIBDIR)/session/conversation_tracker.c $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,mcp_client,mcp/test_mcp_client,$(RALPH_CORE_DEPS) $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,subagent_tool,tools/test_subagent_tool,$(RALPH_CORE_DEPS) $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,incomplete_task_bug,core/test_incomplete_task_bug,$(RALPH_CORE_DEPS) $(COMPLEX_DEPS)))

# Batch link rule for standard tests
STANDARD_TESTS := json_output tools_system vector_db_tool memory_tool memory_mgmt \
    token_manager conversation_compactor rolling_summary model_tools openai_streaming \
    anthropic_streaming messages_array_bug mcp_client subagent_tool incomplete_task_bug

$(foreach t,$(STANDARD_TESTS),$(eval \
$$(TEST_$(t)_TARGET): $$(TEST_$(t)_OBJECTS) $$(ALL_LIBS) ; \
	$$(CXX) -o $$@ $$(TEST_$(t)_OBJECTS) $$(LIBS_STANDARD)))

# test_output needs extra conversation_tracker object (not in its deps)
$(TEST_output_TARGET): $(TEST_output_OBJECTS) $(LIBDIR)/session/conversation_tracker.o $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_output_OBJECTS) $(LIBDIR)/session/conversation_tracker.o $(LIBS_STANDARD)

# =============================================================================
# PYTHON TESTS (need stdlib embedding)
# =============================================================================

$(eval $(call def_test_mixed,python_tool,tools/test_python_tool,$(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,python_integration,tools/test_python_integration,$(COMPLEX_DEPS)))

define PYTHON_TEST_EMBED
	@set -e; \
	if [ ! -d "$(PYTHON_STDLIB_DIR)/lib" ]; then \
		echo "Error: Python stdlib directory '$(PYTHON_STDLIB_DIR)/lib' not found."; \
		exit 1; \
	fi; \
	echo "Embedding Python stdlib into test binary..."; \
	rm -f $(BUILDDIR)/python-test-embed.zip; \
	cd $(PYTHON_STDLIB_DIR) && zip -qr $(CURDIR)/$(BUILDDIR)/python-test-embed.zip lib/; \
	cd $(CURDIR)/$(SRCDIR)/tools && zip -qr $(CURDIR)/$(BUILDDIR)/python-test-embed.zip python_defaults/; \
	zipcopy $(CURDIR)/$(BUILDDIR)/python-test-embed.zip $(CURDIR)/$@; \
	rm -f $(BUILDDIR)/python-test-embed.zip
endef

$(TEST_python_tool_TARGET): $(TEST_python_tool_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_python_tool_OBJECTS) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

$(TEST_python_integration_TARGET): $(TEST_python_integration_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_python_integration_OBJECTS) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

# =============================================================================
# FULL INTEGRATION TESTS
# =============================================================================

$(eval $(call def_test_mixed,http,network/test_http_client,$(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,ralph,core/test_ralph,$(TESTDIR)/mock_api_server.c $(RALPH_CORE_DEPS) $(COMPLEX_DEPS)))
$(eval $(call def_test_mixed,recap,core/test_recap,$(RALPH_CORE_DEPS) $(COMPLEX_DEPS)))

$(TEST_http_TARGET): $(TEST_http_OBJECTS) $(ALL_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_http_OBJECTS) $(LIBS)

$(TEST_ralph_TARGET): $(TEST_ralph_OBJECTS) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_ralph_OBJECTS) $(LIBS) -lpthread

$(TEST_recap_TARGET): $(TEST_recap_OBJECTS) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_recap_OBJECTS) $(LIBS) -lpthread

# =============================================================================
# SPECIAL TESTS (unique structure)
# =============================================================================

# Vector DB test - minimal, just hnswlib
TEST_vector_db_SOURCES := $(TESTDIR)/db/test_vector_db.c $(LIBDIR)/db/vector_db.c $(SRCDIR)/utils/ralph_home.c $(LIB_DB_CPP_SOURCES) $(UNITY)
TEST_vector_db_OBJECTS := $(TESTDIR)/db/test_vector_db.o $(LIBDIR)/db/vector_db.o $(SRCDIR)/utils/ralph_home.o $(LIBDIR)/db/hnswlib_wrapper.o $(TESTDIR)/unity/unity.o
TEST_vector_db_TARGET := $(TESTDIR)/test_vector_db
ALL_TEST_TARGETS += $(TEST_vector_db_TARGET)

$(TEST_vector_db_TARGET): $(TEST_vector_db_OBJECTS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) -o $@ $(TEST_vector_db_OBJECTS) -lpthread -lm

# Document store test
TEST_document_store_SOURCES := $(TESTDIR)/db/test_document_store.c $(LIB_DB_SOURCES) $(LIBDIR)/util/common_utils.c $(SRCDIR)/utils/config.c $(LIBDIR)/util/debug_output.c $(SRCDIR)/utils/ralph_home.c $(EMBEDDING_DEPS) $(NETWORK_DEPS) $(UNITY)
TEST_document_store_OBJECTS := $(TEST_document_store_SOURCES:.c=.o) $(LIB_DB_CPP_SOURCES:.cpp=.o)
TEST_document_store_TARGET := $(TESTDIR)/test_document_store
ALL_TEST_TARGETS += $(TEST_document_store_TARGET)

$(TEST_document_store_TARGET): $(TEST_document_store_OBJECTS) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_document_store_OBJECTS) $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lm -lpthread

# =============================================================================
# TEST EXECUTION
# =============================================================================

TEST_EXECUTION_ORDER := \
    $(TEST_main_TARGET) $(TEST_cli_flags_TARGET) $(TEST_interrupt_TARGET) $(TEST_async_executor_TARGET) $(TEST_darray_TARGET) $(TEST_ptrarray_TARGET) \
    $(TEST_rate_limiter_TARGET) $(TEST_allowlist_TARGET) $(TEST_tool_args_TARGET) $(TEST_gate_prompter_TARGET) \
    $(TEST_ralph_home_TARGET) $(TEST_http_TARGET) $(TEST_http_retry_TARGET) \
    $(TEST_streaming_TARGET) $(TEST_openai_streaming_TARGET) $(TEST_anthropic_streaming_TARGET) \
    $(TEST_output_TARGET) $(TEST_prompt_TARGET) $(TEST_debug_output_TARGET) $(TEST_terminal_TARGET) \
    $(TEST_conversation_TARGET) $(TEST_conversation_vdb_TARGET) $(TEST_tools_system_TARGET) \
    $(TEST_ralph_TARGET) $(TEST_todo_manager_TARGET) $(TEST_tool_param_dsl_TARGET) \
    $(TEST_vector_db_tool_TARGET) $(TEST_memory_tool_TARGET) $(TEST_python_tool_TARGET) \
    $(TEST_python_integration_TARGET) $(TEST_token_manager_TARGET) $(TEST_model_tools_TARGET) \
    $(TEST_conversation_compactor_TARGET) $(TEST_rolling_summary_TARGET) $(TEST_incomplete_task_bug_TARGET) \
    $(TEST_messages_array_bug_TARGET) $(TEST_mcp_client_TARGET) $(TEST_vector_db_TARGET) \
    $(TEST_document_store_TARGET) $(TEST_task_store_TARGET) $(TEST_message_store_TARGET) \
    $(TEST_message_poller_TARGET) $(TEST_notification_formatter_TARGET) $(TEST_sqlite_dal_TARGET) \
    $(TEST_pdf_extractor_TARGET) $(TEST_document_chunker_TARGET) $(TEST_approval_gate_TARGET) \
    $(TEST_atomic_file_TARGET) $(TEST_path_normalize_TARGET) $(TEST_protected_files_TARGET) \
    $(TEST_shell_parser_TARGET) $(TEST_shell_parser_cmd_TARGET) $(TEST_shell_parser_ps_TARGET) \
    $(TEST_subagent_approval_TARGET) $(TEST_subagent_tool_TARGET) $(TEST_json_output_TARGET) \
    $(TEST_verified_file_context_TARGET) $(TEST_approval_gate_integration_TARGET)

test: all
	@echo "Running all tests..."
	@for t in $(TEST_EXECUTION_ORDER); do \
		./$$t || exit 1; \
	done
	@echo "All tests completed"

# Build tests without running them (for when you need to rebuild tests only)
build-tests: $(ALL_TEST_TARGETS)

check: test

# =============================================================================
# VALGRIND
# =============================================================================

# Excluded: HTTP (network), Python (embedded stdlib, including tools_system and ralph which call python_interpreter_init), subagent (fork/exec), message_poller (threads), async_executor (threads)
VALGRIND_TESTS := \
    $(TEST_main_TARGET) $(TEST_cli_flags_TARGET) $(TEST_interrupt_TARGET) $(TEST_darray_TARGET) $(TEST_ptrarray_TARGET) \
    $(TEST_rate_limiter_TARGET) $(TEST_allowlist_TARGET) $(TEST_tool_args_TARGET) $(TEST_gate_prompter_TARGET) \
    $(TEST_ralph_home_TARGET) $(TEST_http_retry_TARGET) $(TEST_streaming_TARGET) \
    $(TEST_openai_streaming_TARGET) $(TEST_anthropic_streaming_TARGET) \
    $(TEST_output_TARGET) $(TEST_prompt_TARGET) $(TEST_terminal_TARGET) $(TEST_conversation_TARGET) \
    $(TEST_conversation_vdb_TARGET) \
    $(TEST_todo_manager_TARGET) $(TEST_tool_param_dsl_TARGET) $(TEST_vector_db_tool_TARGET) \
    $(TEST_memory_tool_TARGET) $(TEST_token_manager_TARGET) $(TEST_conversation_compactor_TARGET) $(TEST_rolling_summary_TARGET) \
    $(TEST_model_tools_TARGET) $(TEST_vector_db_TARGET) $(TEST_sqlite_dal_TARGET) $(TEST_task_store_TARGET) \
    $(TEST_message_store_TARGET) $(TEST_notification_formatter_TARGET) \
    $(TEST_pdf_extractor_TARGET) $(TEST_document_chunker_TARGET) \
    $(TEST_approval_gate_TARGET) $(TEST_atomic_file_TARGET) $(TEST_path_normalize_TARGET) \
    $(TEST_protected_files_TARGET) $(TEST_shell_parser_TARGET) $(TEST_shell_parser_cmd_TARGET) \
    $(TEST_shell_parser_ps_TARGET) $(TEST_subagent_approval_TARGET) $(TEST_json_output_TARGET) \
    $(TEST_verified_file_context_TARGET) $(TEST_approval_gate_integration_TARGET)

check-valgrind: $(ALL_TEST_TARGETS)
	@echo "Running valgrind tests (excluding HTTP and Python tests)..."
	@for t in $(VALGRIND_TESTS); do \
		valgrind $(VALGRIND_FLAGS) ./$$t.aarch64.elf || exit 1; \
	done
	@echo "Valgrind tests completed (subagent tests excluded - see AGENTS.md)"

.PHONY: test check check-valgrind build-tests
