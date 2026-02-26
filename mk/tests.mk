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

# Template for libagent-linked test (test .c + optional extras, linked against libagent.a)
# $(1) = test name (lowercase, no prefix)
# $(2) = test file path (relative to TESTDIR, no .c)
# $(3) = additional source files (mocks, stubs — listed BEFORE libagent.a to win symbol resolution)
define def_test_lib
TEST_$(1)_SOURCES := $$(TESTDIR)/$(2).c $(3) $$(UNITY)
TEST_$(1)_OBJECTS := $$(TEST_$(1)_SOURCES:.c=.o)
TEST_$(1)_TARGET := $$(TESTDIR)/test_$(1)
ALL_TEST_TARGETS += $$(TEST_$(1)_TARGET)
endef

# =============================================================================
# MINIMAL TESTS (no external libraries)
# =============================================================================

$(eval $(call def_test,main,ralph/test_main,))
$(eval $(call def_test,cli_flags,ralph/test_cli_flags,))
$(eval $(call def_test,interrupt,ralph/test_interrupt,$(LIBDIR)/util/interrupt.c))
$(eval $(call def_test,async_executor,ralph/test_async_executor,$(LIBDIR)/agent/async_executor.c $(LIBDIR)/util/interrupt.c $(LIBDIR)/util/debug_output.c $(LIBDIR)/ipc/pipe_notifier.c $(TESTDIR)/stubs/ralph_stub.c))
$(eval $(call def_test,pipe_notifier,utils/test_pipe_notifier,$(LIBDIR)/ipc/pipe_notifier.c))
$(eval $(call def_test,agent_identity,ralph/test_agent_identity,$(LIBDIR)/ipc/agent_identity.c))
$(eval $(call def_test,prompt,utils/test_prompt_loader,$(LIBDIR)/util/prompt_loader.c $(LIBDIR)/util/app_home.c $(LIBDIR)/util/config.c $(LIBDIR)/util/debug_output.c))
$(eval $(call def_test,app_home,utils/test_app_home,$(LIBDIR)/util/app_home.c))
$(eval $(call def_test,todo_manager,tools/test_todo_manager,$(LIBDIR)/tools/todo_manager.c))
$(eval $(call def_test,document_chunker,test_document_chunker,$(LIBDIR)/util/document_chunker.c $(LIBDIR)/util/common_utils.c))
$(eval $(call def_test,streaming,network/test_streaming,$(LIBDIR)/network/streaming.c))
$(eval $(call def_test,darray,test_darray,))
$(eval $(call def_test,ptrarray,test_ptrarray,))
$(eval $(call def_test,rate_limiter,policy/test_rate_limiter,$(LIBDIR)/policy/rate_limiter.c))
$(eval $(call def_test,allowlist,policy/test_allowlist,$(LIBDIR)/policy/allowlist.c $(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_cmd.c $(LIBDIR)/policy/shell_parser_ps.c))
$(eval $(call def_test,tool_args,test_tool_args,$(LIBDIR)/policy/tool_args.c))
$(eval $(call def_test,gate_prompter,policy/test_gate_prompter,$(LIBDIR)/policy/gate_prompter.c))
$(eval $(call def_test,tool_cache,tools/test_tool_cache,$(LIBDIR)/tools/tool_cache.c))
$(eval $(call def_test,prompt_mode,agent/test_prompt_mode,$(LIBDIR)/agent/prompt_mode.c))

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
    $(LIBDIR)/util/app_home.c \
    $(LIBDIR)/util/json_escape.c

$(eval $(call def_test,approval_gate,policy/test_approval_gate,$(GATE_DEPS)))
$(eval $(call def_test,atomic_file,policy/test_atomic_file,$(LIBDIR)/policy/atomic_file.c))
$(eval $(call def_test,path_normalize,policy/test_path_normalize,$(LIBDIR)/policy/path_normalize.c))
$(eval $(call def_test,verified_file_context,test_verified_file_context,$(LIBDIR)/policy/verified_file_context.c $(LIBDIR)/policy/atomic_file.c $(LIBDIR)/policy/path_normalize.c))
$(eval $(call def_test,protected_files,policy/test_protected_files,$(LIBDIR)/policy/protected_files.c $(LIBDIR)/policy/path_normalize.c $(LIBDIR)/util/app_home.c))
$(eval $(call def_test,shell_parser,policy/test_shell_parser,$(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_cmd.c $(LIBDIR)/policy/shell_parser_ps.c))
$(eval $(call def_test,shell_parser_cmd,policy/test_shell_parser_cmd,$(LIBDIR)/policy/shell_parser_cmd.c $(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_ps.c))
$(eval $(call def_test,shell_parser_ps,policy/test_shell_parser_ps,$(LIBDIR)/policy/shell_parser_ps.c $(LIBDIR)/policy/shell_parser.c $(LIBDIR)/policy/shell_parser_cmd.c))
$(eval $(call def_test,subagent_approval,policy/test_subagent_approval,$(GATE_DEPS)))
$(eval $(call def_test,approval_gate_integration,policy/test_approval_gate_integration,$(GATE_DEPS) $(TESTDIR)/stubs/python_tool_stub.c))

$(TEST_main_TARGET): $(TEST_main_OBJECTS)
	$(CC) -o $@ $(TEST_main_OBJECTS)

$(TEST_cli_flags_TARGET): $(TEST_cli_flags_OBJECTS) $(BUILDDIR)/.scaffold-linked
	$(CC) -o $@ $(TEST_cli_flags_OBJECTS)

$(TEST_interrupt_TARGET): $(TEST_interrupt_OBJECTS)
	$(CC) -o $@ $(TEST_interrupt_OBJECTS)

$(TEST_async_executor_TARGET): $(TEST_async_executor_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_async_executor_OBJECTS) $(CJSON_LIB) -lpthread

$(TEST_pipe_notifier_TARGET): $(TEST_pipe_notifier_OBJECTS)
	$(CC) -o $@ $(TEST_pipe_notifier_OBJECTS)

$(TEST_agent_identity_TARGET): $(TEST_agent_identity_OBJECTS)
	$(CC) -o $@ $(TEST_agent_identity_OBJECTS) -lpthread

$(TEST_prompt_TARGET): $(TEST_prompt_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_prompt_OBJECTS) $(CJSON_LIB)

$(TEST_app_home_TARGET): $(TEST_app_home_OBJECTS)
	$(CC) -o $@ $(TEST_app_home_OBJECTS)

$(TEST_todo_manager_TARGET): $(TEST_todo_manager_OBJECTS)
	$(CC) -o $@ $(TEST_todo_manager_OBJECTS)

$(TEST_document_chunker_TARGET): $(TEST_document_chunker_OBJECTS)
	$(CC) -o $@ $(TEST_document_chunker_OBJECTS)

$(TEST_streaming_TARGET): $(TEST_streaming_OBJECTS)
	$(CC) -o $@ $(TEST_streaming_OBJECTS)

$(TEST_darray_TARGET): $(TEST_darray_OBJECTS)
	$(CC) -o $@ $(TEST_darray_OBJECTS)

$(TEST_ptrarray_TARGET): $(TEST_ptrarray_OBJECTS)
	$(CC) -o $@ $(TEST_ptrarray_OBJECTS)

$(TEST_rate_limiter_TARGET): $(TEST_rate_limiter_OBJECTS)
	$(CC) -o $@ $(TEST_rate_limiter_OBJECTS)

$(TEST_allowlist_TARGET): $(TEST_allowlist_OBJECTS)
	$(CC) -o $@ $(TEST_allowlist_OBJECTS)

$(TEST_tool_args_TARGET): $(TEST_tool_args_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_tool_args_OBJECTS) $(CJSON_LIB)

$(TEST_gate_prompter_TARGET): $(TEST_gate_prompter_OBJECTS)
	$(CC) -o $@ $(TEST_gate_prompter_OBJECTS)

$(TEST_tool_cache_TARGET): $(TEST_tool_cache_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_tool_cache_OBJECTS) $(CJSON_LIB) -lpthread

$(TEST_prompt_mode_TARGET): $(TEST_prompt_mode_OBJECTS)
	$(CC) -o $@ $(TEST_prompt_mode_OBJECTS)

$(TEST_approval_gate_TARGET): $(TEST_approval_gate_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_approval_gate_OBJECTS) $(CJSON_LIB)

$(TEST_atomic_file_TARGET): $(TEST_atomic_file_OBJECTS)
	$(CC) -o $@ $(TEST_atomic_file_OBJECTS)

$(TEST_path_normalize_TARGET): $(TEST_path_normalize_OBJECTS)
	$(CC) -o $@ $(TEST_path_normalize_OBJECTS)

$(TEST_protected_files_TARGET): $(TEST_protected_files_OBJECTS)
	$(CC) -o $@ $(TEST_protected_files_OBJECTS)

$(TEST_verified_file_context_TARGET): $(TEST_verified_file_context_OBJECTS)
	$(CC) -o $@ $(TEST_verified_file_context_OBJECTS)

$(TEST_shell_parser_TARGET): $(TEST_shell_parser_OBJECTS)
	$(CC) -o $@ $(TEST_shell_parser_OBJECTS)

$(TEST_shell_parser_cmd_TARGET): $(TEST_shell_parser_cmd_OBJECTS)
	$(CC) -o $@ $(TEST_shell_parser_cmd_OBJECTS)

$(TEST_shell_parser_ps_TARGET): $(TEST_shell_parser_ps_OBJECTS)
	$(CC) -o $@ $(TEST_shell_parser_ps_OBJECTS)

$(TEST_subagent_approval_TARGET): $(TEST_subagent_approval_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_subagent_approval_OBJECTS) $(CJSON_LIB)

$(TEST_approval_gate_integration_TARGET): $(TEST_approval_gate_integration_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_approval_gate_integration_OBJECTS) $(CJSON_LIB)

# =============================================================================
# UPDATER TEST
# =============================================================================

$(eval $(call def_test,updater,updater/test_updater,\
    $(TESTDIR)/updater/mock_http.c $(LIBDIR)/updater/updater.c \
    $(LIBDIR)/util/executable_path.c $(LIBDIR)/util/app_home.c))

$(TEST_updater_TARGET): $(TEST_updater_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_updater_OBJECTS) $(CJSON_LIB)

# =============================================================================
# CJSON TESTS
# =============================================================================

$(eval $(call def_test,config,utils/test_config,$(LIBDIR)/util/config.c $(LIBDIR)/util/app_home.c $(LIBDIR)/util/debug_output.c))
$(eval $(call def_test,debug_output,utils/test_debug_output,$(LIBDIR)/util/debug_output.c))
$(eval $(call def_test,spinner,utils/test_spinner,$(LIBDIR)/ui/terminal.c $(LIBDIR)/ui/spinner.c $(LIBDIR)/util/common_utils.c $(TESTDIR)/stubs/output_formatter_stub.c))
$(eval $(call def_test,terminal,utils/test_terminal,$(LIBDIR)/ui/terminal.c $(LIBDIR)/util/common_utils.c $(TESTDIR)/stubs/output_formatter_stub.c))
$(eval $(call def_test_lib,slash_commands,ui/test_slash_commands,))

$(TEST_config_TARGET): $(TEST_config_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_config_OBJECTS) $(CJSON_LIB)

$(TEST_debug_output_TARGET): $(TEST_debug_output_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_debug_output_OBJECTS) $(CJSON_LIB)

$(TEST_spinner_TARGET): $(TEST_spinner_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_spinner_OBJECTS) $(CJSON_LIB) -lpthread

$(TEST_terminal_TARGET): $(TEST_terminal_OBJECTS)
	$(CC) -o $@ $(TEST_terminal_OBJECTS)

# =============================================================================
# PDF TEST
# =============================================================================

$(eval $(call def_test,pdf_extractor,pdf/test_pdf_extractor,$(LIBDIR)/pdf/pdf_extractor.c))

$(TEST_pdf_extractor_TARGET): $(TEST_pdf_extractor_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB)
	$(CC) -o $@ $(TEST_pdf_extractor_OBJECTS) $(PDFIO_LIB) $(ZLIB_LIB) -lm

# =============================================================================
# HTTP RETRY TEST
# =============================================================================

$(eval $(call def_test,http_retry,network/test_http_retry,$(LIBDIR)/network/api_error.c $(LIBDIR)/util/config.c $(LIBDIR)/util/app_home.c $(LIBDIR)/util/debug_output.c))

$(TEST_http_retry_TARGET): $(TEST_http_retry_OBJECTS) $(CJSON_LIB) $(LIBS_MBEDTLS)
	$(CC) -o $@ $(TEST_http_retry_OBJECTS) $(CJSON_LIB) $(LIBS_MBEDTLS) -lm

# =============================================================================
# SQLITE TESTS
# =============================================================================

$(eval $(call def_test,sqlite_dal,db/test_sqlite_dal,$(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/app_home.c))
$(eval $(call def_test,task_store,db/test_task_store,$(LIBDIR)/db/task_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/uuid_utils.c $(LIBDIR)/util/app_home.c))
$(eval $(call def_test,message_store,db/test_message_store,$(LIBDIR)/ipc/message_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/uuid_utils.c $(LIBDIR)/util/app_home.c))

$(TEST_sqlite_dal_TARGET): $(TEST_sqlite_dal_OBJECTS) $(SQLITE_LIB)
	$(CC) -o $@ $(TEST_sqlite_dal_OBJECTS) $(SQLITE_LIB) -lpthread -lm

$(TEST_task_store_TARGET): $(TEST_task_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB)
	$(CC) -o $@ $(TEST_task_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lpthread -lm

$(TEST_message_store_TARGET): $(TEST_message_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB)
	$(CC) -o $@ $(TEST_message_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lpthread -lm

$(eval $(call def_test,goal_store,db/test_goal_store,$(LIBDIR)/db/goal_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/uuid_utils.c $(LIBDIR)/util/app_home.c))
$(eval $(call def_test,action_store,db/test_action_store,$(LIBDIR)/db/action_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/orchestrator/goap_state.c $(LIBDIR)/util/uuid_utils.c $(LIBDIR)/util/app_home.c))

$(TEST_goal_store_TARGET): $(TEST_goal_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB)
	$(CC) -o $@ $(TEST_goal_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) -lpthread -lm

$(TEST_action_store_TARGET): $(TEST_action_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_action_store_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB) -lpthread -lm

$(eval $(call def_test,orchestrator,orchestrator/test_orchestrator,$(LIBDIR)/orchestrator/orchestrator.c $(LIBDIR)/db/goal_store.c $(LIBDIR)/db/sqlite_dal.c $(LIBDIR)/util/uuid_utils.c $(LIBDIR)/util/app_home.c $(LIBDIR)/util/executable_path.c $(LIBDIR)/util/process_spawn.c $(LIBDIR)/util/debug_output.c))

$(TEST_orchestrator_TARGET): $(TEST_orchestrator_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_orchestrator_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB) -lpthread -lm

$(eval $(call def_test,role_prompts,orchestrator/test_role_prompts,$(LIBDIR)/orchestrator/role_prompts.c $(LIBDIR)/util/app_home.c))

$(TEST_role_prompts_TARGET): $(TEST_role_prompts_OBJECTS)
	$(CC) -o $@ $(TEST_role_prompts_OBJECTS)

# Messaging deps (use stubs that conflict with libagent.a symbols)
MESSAGING_DEPS := \
    $(LIBDIR)/ipc/message_poller.c \
    $(LIBDIR)/ipc/notification_formatter.c \
    $(LIBDIR)/ipc/message_store.c \
    $(LIBDIR)/db/sqlite_dal.c \
    $(LIBDIR)/util/uuid_utils.c \
    $(LIBDIR)/util/app_home.c \
    $(LIBDIR)/util/common_utils.c \
    $(LIBDIR)/ipc/pipe_notifier.c \
    $(TESTDIR)/stubs/services_stub.c \
    $(TESTDIR)/stubs/output_formatter_stub.c

$(eval $(call def_test,message_poller,messaging/test_message_poller,$(MESSAGING_DEPS)))
$(eval $(call def_test,notification_formatter,messaging/test_notification_formatter,$(MESSAGING_DEPS)))

$(TEST_message_poller_TARGET): $(TEST_message_poller_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_message_poller_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB) -lpthread -lm

$(TEST_notification_formatter_TARGET): $(TEST_notification_formatter_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_notification_formatter_OBJECTS) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(CJSON_LIB) -lpthread -lm

# =============================================================================
# VECTOR DB TEST (minimal, just hnswlib)
# =============================================================================

TEST_vector_db_SOURCES := $(TESTDIR)/db/test_vector_db.c $(LIBDIR)/db/vector_db.c $(LIBDIR)/util/app_home.c $(LIB_DB_CPP_SOURCES) $(UNITY)
TEST_vector_db_OBJECTS := $(TESTDIR)/db/test_vector_db.o $(LIBDIR)/db/vector_db.o $(LIBDIR)/util/app_home.o $(LIBDIR)/db/hnswlib_wrapper.o $(TESTDIR)/unity/unity.o
TEST_vector_db_TARGET := $(TESTDIR)/test_vector_db
ALL_TEST_TARGETS += $(TEST_vector_db_TARGET)

$(TEST_vector_db_TARGET): $(TEST_vector_db_OBJECTS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) -o $@ $(TEST_vector_db_OBJECTS) -lpthread -lm

# =============================================================================
# LIBRALPH-LINKED TESTS
# =============================================================================

# Mock embeddings sources for tests that need mocked embedding API
MOCK_EMBEDDINGS_SOURCES := $(TESTDIR)/mock_api_server.c $(TESTDIR)/mock_embeddings.c $(TESTDIR)/mock_embeddings_server.c

$(eval $(call def_test_lib,tool_param_dsl,tools/test_tool_param_dsl,))
$(eval $(call def_test_lib,json_output,utils/test_json_output,))
$(eval $(call def_test_lib,output,utils/test_output_formatter,))
$(eval $(call def_test_lib,tools_system,tools/test_tools_system,))
$(eval $(call def_test_lib,vector_db_tool,tools/test_vector_db_tool,$(MOCK_EMBEDDINGS_SOURCES)))
$(eval $(call def_test_lib,memory_tool,tools/test_memory_tool,$(MOCK_EMBEDDINGS_SOURCES)))
$(eval $(call def_test_lib,memory_mgmt,test_memory_management,))
$(eval $(call def_test_lib,token_manager,session/test_token_manager,))
$(eval $(call def_test_lib,conversation_compactor,session/test_conversation_compactor,))
$(eval $(call def_test_lib,rolling_summary,session/test_rolling_summary,))
$(eval $(call def_test_lib,model_tools,llm/test_model_tools,))
$(eval $(call def_test_lib,openai_streaming,llm/test_openai_streaming,))
$(eval $(call def_test_lib,anthropic_streaming,llm/test_anthropic_streaming,))
$(eval $(call def_test_lib,messages_array_bug,network/test_messages_array_bug,))
$(eval $(call def_test_lib,mcp_client,mcp/test_mcp_client,))
$(eval $(call def_test_lib,subagent_tool,tools/test_subagent_tool,))
$(eval $(call def_test_lib,incomplete_task_bug,ralph/test_incomplete_task_bug,))
$(eval $(call def_test_lib,conversation,session/test_conversation_tracker,))
$(eval $(call def_test_lib,conversation_vdb,session/test_conversation_vector_db,$(TESTDIR)/mock_embeddings.c $(TESTDIR)/mock_embeddings_service.c))
$(eval $(call def_test_lib,tool_calls_not_stored,session/test_tool_calls_not_stored,$(TESTDIR)/mock_api_server.c $(TESTDIR)/mock_embeddings.c $(TESTDIR)/mock_embeddings_server.c))
$(eval $(call def_test_lib,document_store,db/test_document_store,))
$(eval $(call def_test_lib,http,network/test_http_client,))
$(eval $(call def_test_lib,ralph,ralph/test_ralph,$(TESTDIR)/mock_api_server.c))
$(eval $(call def_test_lib,message_dispatcher,ralph/test_message_dispatcher,))
$(eval $(call def_test_lib,message_processor,ralph/test_message_processor,))
$(eval $(call def_test_lib,recap,ralph/test_recap,))
$(eval $(call def_test_lib,parallel_batch,agent/test_parallel_batch,))
$(eval $(call def_test_lib,model_commands,ui/test_model_commands,))
$(eval $(call def_test_lib,task_commands,ui/test_task_commands,))
$(eval $(call def_test_lib,agent_commands,ui/test_agent_commands,))
$(eval $(call def_test_lib,goal_commands,ui/test_goal_commands,))
$(eval $(call def_test_lib,mode_tool,tools/test_mode_tool,))
$(eval $(call def_test_lib,mode_commands,ui/test_mode_commands,))
$(eval $(call def_test_lib,context_mode_injection,agent/test_context_mode_injection,))
$(eval $(call def_test_lib,python_tool,tools/test_python_tool,$(TOOL_SOURCES)))
$(eval $(call def_test_lib,python_integration,tools/test_python_integration,$(TOOL_SOURCES)))
$(eval $(call def_test_lib,http_python,network/test_http_python,$(SRCDIR)/tools/http_python.c))
$(eval $(call def_test_lib,sys_python,tools/test_sys_python,$(SRCDIR)/tools/sys_python.c))
$(eval $(call def_test_lib,image_attachment,network/test_image_attachment,))
$(eval $(call def_test_lib,goap_tools,tools/test_goap_tools,))
$(eval $(call def_test_lib,orchestrator_tool,tools/test_orchestrator_tool,))
$(eval $(call def_test_lib,goap_lifecycle,orchestrator/test_goap_lifecycle,))
$(eval $(call def_test_lib,goap_state,orchestrator/test_goap_state,))
$(eval $(call def_test,plugin_protocol,plugin/test_plugin_protocol,$(LIBDIR)/plugin/plugin_protocol.c))
$(eval $(call def_test_lib,plugin_manager,plugin/test_plugin_manager,))
$(eval $(call def_test_lib,hook_dispatcher,plugin/test_hook_dispatcher,))
$(eval $(call def_test_lib,plugin_integration,plugin/test_plugin_integration,))

$(TEST_plugin_protocol_TARGET): $(TEST_plugin_protocol_OBJECTS) $(CJSON_LIB)
	$(CC) -o $@ $(TEST_plugin_protocol_OBJECTS) $(CJSON_LIB)

# Batch link rule for libagent-linked tests
LIBAGENT_TESTS := tool_param_dsl json_output output tools_system vector_db_tool memory_tool \
    memory_mgmt token_manager conversation_compactor rolling_summary model_tools \
    openai_streaming anthropic_streaming messages_array_bug mcp_client subagent_tool \
    incomplete_task_bug conversation conversation_vdb tool_calls_not_stored document_store \
    message_dispatcher message_processor recap parallel_batch model_commands \
    slash_commands task_commands agent_commands goal_commands mode_tool mode_commands \
    context_mode_injection image_attachment goap_tools orchestrator_tool goap_lifecycle \
    goap_state plugin_manager hook_dispatcher plugin_integration

$(foreach t,$(LIBAGENT_TESTS),$(eval \
$$(TEST_$(t)_TARGET): $$(TEST_$(t)_OBJECTS) $$(LIBAGENT) $$(ALL_LIBS) ; \
	$$(CXX) -o $$@ $$(TEST_$(t)_OBJECTS) $$(LIBAGENT) $$(LIBS_STANDARD)))

# http test needs LDFLAGS for library search paths
$(TEST_http_TARGET): $(TEST_http_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_http_OBJECTS) $(LIBAGENT) $(LIBS) -lpthread

# ralph test needs LDFLAGS and mock_api_server
$(TEST_ralph_TARGET): $(TEST_ralph_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_ralph_OBJECTS) $(LIBAGENT) $(LIBS) -lpthread

# =============================================================================
# PYTHON TESTS (need stdlib embedding)
# =============================================================================

define PYTHON_TEST_EMBED
	@set -e; \
	if [ ! -d "$(PYTHON_STDLIB_DIR)/lib" ]; then \
		echo "Error: Python stdlib directory '$(PYTHON_STDLIB_DIR)/lib' not found."; \
		exit 1; \
	fi; \
	echo "Embedding Python stdlib into test binary..."; \
	EMBED_ZIP=$(CURDIR)/$(BUILDDIR)/python-embed-$$(basename $@).zip; \
	rm -f $$EMBED_ZIP; \
	cd $(PYTHON_STDLIB_DIR) && zip -qr $$EMBED_ZIP lib/; \
	cd $(CURDIR)/$(SRCDIR)/tools && zip -qr $$EMBED_ZIP python_defaults/; \
	zipcopy $$EMBED_ZIP $(CURDIR)/$@; \
	rm -f $$EMBED_ZIP
endef

$(TEST_python_tool_TARGET): $(TEST_python_tool_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_python_tool_OBJECTS) $(LIBAGENT) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

$(TEST_python_integration_TARGET): $(TEST_python_integration_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_python_integration_OBJECTS) $(LIBAGENT) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

$(TEST_http_python_TARGET): $(TEST_http_python_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_http_python_OBJECTS) $(LIBAGENT) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

$(TEST_sys_python_TARGET): $(TEST_sys_python_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_sys_python_OBJECTS) $(LIBAGENT) $(LIBS_STANDARD)
	$(PYTHON_TEST_EMBED)

# =============================================================================
# FLAT HEADER + STATIC LIBRARY LINKAGE TEST
# =============================================================================

# Compiles against the generated out/libagent.h (not individual lib/ headers)
# to verify the flat header is self-contained and the library links cleanly.
TEST_libagent_header_SOURCES := $(TESTDIR)/test_libagent_header.c $(UNITY)
TEST_libagent_header_OBJECTS := $(TESTDIR)/test_libagent_header.o $(TESTDIR)/unity/unity.o
TEST_libagent_header_TARGET := $(TESTDIR)/test_libagent_header
ALL_TEST_TARGETS += $(TEST_libagent_header_TARGET)

$(TESTDIR)/test_libagent_header.o: $(TESTDIR)/test_libagent_header.c $(LIBAGENT_HEADER)
	$(CC) $(CFLAGS) -I$(OUTDIR) -I$(TESTDIR)/unity -c $< -o $@

$(TEST_libagent_header_TARGET): $(TEST_libagent_header_OBJECTS) $(LIBAGENT) $(ALL_LIBS)
	$(CXX) -o $@ $(TEST_libagent_header_OBJECTS) $(LIBAGENT) $(LIBS_STANDARD)

# =============================================================================
# TEST EXECUTION
# =============================================================================

test: all
	@echo "Running all tests..."
	@for t in $(ALL_TEST_TARGETS); do \
		./$$t || exit 1; \
	done
	@echo "All tests completed"

# Build tests without running them (for when you need to rebuild tests only)
build-tests: $(ALL_TEST_TARGETS)

check: test

# =============================================================================
# VALGRIND
# =============================================================================

# Excluded: HTTP (network), Python (embedded stdlib/init), subagent (fork/exec),
# threads (message_poller, async_executor), tools_system/ralph (python_interpreter_init),
# recap (uses timeout + SIGPIPE)
VALGRIND_EXCLUDED := $(TEST_http_TARGET) $(TEST_python_tool_TARGET) $(TEST_python_integration_TARGET) \
    $(TEST_http_python_TARGET) $(TEST_sys_python_TARGET) \
    $(TEST_tools_system_TARGET) $(TEST_ralph_TARGET) $(TEST_recap_TARGET) \
    $(TEST_subagent_tool_TARGET) $(TEST_message_poller_TARGET) $(TEST_async_executor_TARGET) \
    $(TEST_parallel_batch_TARGET) $(TEST_orchestrator_TARGET) $(TEST_plugin_integration_TARGET)

VALGRIND_TESTS := $(filter-out $(VALGRIND_EXCLUDED),$(ALL_TEST_TARGETS))

check-valgrind: $(ALL_TEST_TARGETS)
	@echo "Running valgrind tests (excluding HTTP, Python, subagent, and thread tests)..."
	@for t in $(VALGRIND_TESTS); do \
		valgrind $(VALGRIND_FLAGS) ./$$t.aarch64.elf || exit 1; \
	done
	@echo "Valgrind tests completed"

.PHONY: test check check-valgrind build-tests
