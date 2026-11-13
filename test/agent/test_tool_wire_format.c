/*
 * Test: tool wire format must follow the endpoint, not the model name
 *
 * The endpoint (api_url / api_type / provider) decides the wire format; the
 * model name only describes the model. Before the fix, the tool wire format
 * was selected from the model name, so a claude model (or any model missing
 * from the capability table) pointed at an OpenAI-compatible gateway such as
 * OpenRouter produced Anthropic-format (or no) tool definitions and failed
 * to parse the OpenAI-format tool calls the gateway returned.
 *
 * Covers the three format decision points:
 *   1. tools array in the request payload   (build_json_payload_common)
 *   2. tool calls parsed from the response  (api_round_trip_execute)
 *   3. assistant tool turn in history       (conversation_append_assistant)
 */
#include "unity.h"
#include "agent/session.h"
#include "agent/session_configurator.h"
#include "agent/api_round_trip.h"
#include "agent/conversation_state.h"
#include "network/api_common.h"
#include "session/conversation_tracker.h"
#include "tools/tools_system.h"
#include "tools/tool_format.h"
#include "llm/model_capabilities.h"
#include "llm/llm_client.h"
#include "util/app_home.h"
#include "util/config.h"
#include "mock_api_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ToolRegistry g_tools;
static int g_tools_live;

static int dummy_execute(const ToolCall* call, ToolResult* result) {
    (void)call;
    (void)result;
    return 0;
}

static void register_test_tool(void) {
    if (g_tools_live) return;
    memset(&g_tools, 0, sizeof(g_tools));
    init_tool_registry(&g_tools);
    ToolParameter params[] = {
        { .name = "path", .type = "string", .description = "Directory path", .required = 1 },
    };
    TEST_ASSERT_EQUAL(0, register_tool(&g_tools, "list_dir",
                                       "List files in a directory",
                                       params, 1, dummy_execute));
    g_tools_live = 1;
}

void setUp(void) {
    app_home_init(NULL);
    TEST_ASSERT_EQUAL(0, llm_client_init());
    register_test_tool();
}

static char* openai_tool_call_response(const char* request_body, void* user_data) {
    (void)request_body;
    (void)user_data;
    static const char* body =
        "{\"id\":\"chatcmpl-mock\",\"object\":\"chat.completion\","
        "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
        "\"content\":\"I'll list the directory.\","
        "\"tool_calls\":[{\"id\":\"call_abc\",\"type\":\"function\","
        "\"function\":{\"name\":\"list_dir\",\"arguments\":\"{\\\"path\\\":\\\".\\\"}\"}}]},"
        "\"finish_reason\":\"tool_calls\"}],"
        "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,\"total_tokens\":15}}";
    return strdup(body);
}

static MockAPIServer g_server;
static int g_server_running;

static void start_mock_server(void) {
    static MockAPIResponse response;

    memset(&g_server, 0, sizeof(g_server));
    memset(&response, 0, sizeof(response));
    g_server.port = MOCK_SERVER_DEFAULT_PORT;
    response.endpoint = "/v1/chat/completions";
    response.method = "POST";
    response.response_code = 200;
    response.callback = openai_tool_call_response;
    g_server.responses = &response;
    g_server.response_count = 1;

    TEST_ASSERT_EQUAL(0, mock_api_server_start(&g_server));
    TEST_ASSERT_EQUAL(0, mock_api_server_wait_ready(&g_server, 1000));
    g_server_running = 1;
}

void tearDown(void) {
    if (g_server_running) {
        mock_api_server_stop(&g_server);
        g_server_running = 0;
    }
    if (g_tools_live) {
        cleanup_tool_registry(&g_tools);
        g_tools_live = 0;
    }
    llm_client_cleanup();
    app_home_cleanup();
}

/* ---------- 1. Request payload: tools array format ---------- */

void test_payload_tools_openai_wire_claude_model(void) {
    ConversationHistory conv;
    init_conversation_history(&conv);

    /* OpenAI-compatible endpoint (e.g. OpenRouter) with a claude model */
    char* payload = build_json_payload_common("anthropic/claude-opus-5.5", NULL, &conv,
                                              "list files", "max_tokens", 100,
                                              &g_tools, format_openai_message, 0);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"type\":\"function\""),
                                 "claude model on OpenAI wire must use OpenAI tool format");
    TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"input_schema\""),
                             "OpenAI wire must not carry Anthropic input_schema tools");

    free(payload);
    cleanup_conversation_history(&conv);
}

void test_payload_tools_openai_wire_unknown_model(void) {
    ConversationHistory conv;
    init_conversation_history(&conv);

    /* A model missing from the capability table must still get tools */
    char* payload = build_json_payload_common("acme/supermodel-9000", NULL, &conv,
                                              "list files", "max_tokens", 100,
                                              &g_tools, format_openai_message, 0);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"tools\""),
                                 "unknown model on OpenAI wire must still send tools");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"type\":\"function\""),
                                 "unknown model on OpenAI wire must use OpenAI tool format");

    free(payload);
    cleanup_conversation_history(&conv);
}

void test_payload_tools_anthropic_wire_claude_model(void) {
    ConversationHistory conv;
    init_conversation_history(&conv);

    /* The real Anthropic endpoint keeps Anthropic tool format */
    char* payload = build_json_payload_common("anthropic/claude-opus-5.5", NULL, &conv,
                                              "list files", "max_tokens", 100,
                                              &g_tools, format_anthropic_message, 1);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"input_schema\""),
                                 "Anthropic wire must use Anthropic tool format");
    TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"type\":\"function\""),
                             "Anthropic wire must not use OpenAI tool format");

    free(payload);
    cleanup_conversation_history(&conv);
}

/* ---------- 2. Response: tool call parsing ---------- */

void test_round_trip_parses_openai_tool_calls_claude_model(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    session.model_registry = get_model_registry();
    session.session_data.config.api_url = strdup("http://127.0.0.1:8888/v1/chat/completions");
    session.session_data.config.api_type = API_TYPE_LOCAL;
    session.session_data.config.model = strdup("anthropic/claude-opus-5.5");
    session.session_data.config.max_tokens_param = "max_tokens";
    init_conversation_history(&session.session_data.conversation);

    start_mock_server();

    LLMRoundTripResult result;
    memset(&result, 0, sizeof(result));

    TEST_ASSERT_EQUAL(0, api_round_trip_execute(&session, "list the files", 100, &result));

    TEST_ASSERT_EQUAL_MESSAGE(1, result.tool_call_count,
                              "OpenAI-format tool call must be parsed for claude model on OpenAI wire");
    if (result.tool_call_count > 0) {
        TEST_ASSERT_EQUAL_STRING("call_abc", result.tool_calls[0].id);
        TEST_ASSERT_EQUAL_STRING("list_dir", result.tool_calls[0].name);
    }
    TEST_ASSERT_EQUAL_STRING("I'll list the directory.", result.parsed.response_content);

    api_round_trip_cleanup(&result);
    cleanup_conversation_history(&session.session_data.conversation);
    free(session.session_data.config.api_url);
    free(session.session_data.config.model);
}

/* ---------- 3. History: assistant tool turn format ---------- */

static void append_tool_turn(AgentSession* session, APIType api_type, const char* model) {
    ToolCall call = {
        .id = (char*)"call_xyz",
        .name = (char*)"list_dir",
        .arguments = (char*)"{}",
    };
    session->model_registry = get_model_registry();
    session->session_data.config.api_type = api_type;
    free(session->session_data.config.model);
    session->session_data.config.model = strdup(model);

    TEST_ASSERT_EQUAL(0, conversation_append_assistant(session, "checking the directory",
                                                       &call, 1));
}

void test_history_openai_wire_claude_model(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    append_tool_turn(&session, API_TYPE_LOCAL, "anthropic/claude-opus-5.5");

    const ConversationMessage* last = &session.session_data.conversation.data[
        session.session_data.conversation.count - 1];
    TEST_ASSERT_EQUAL_STRING("assistant", last->role);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(last->content, "\"tool_calls\""),
                                 "OpenAI wire history must keep the tool_calls array so "
                                 "subsequent tool results can reference it");
    TEST_ASSERT_NOT_NULL(strstr(last->content, "call_xyz"));

    cleanup_conversation_history(&session.session_data.conversation);
    free(session.session_data.config.model);
}

void test_history_openai_wire_unknown_model(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    append_tool_turn(&session, API_TYPE_LOCAL, "acme/supermodel-9000");

    const ConversationMessage* last = &session.session_data.conversation.data[
        session.session_data.conversation.count - 1];
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(last->content, "\"tool_calls\""),
                                 "unknown model on OpenAI wire must keep the tool_calls array");

    cleanup_conversation_history(&session.session_data.conversation);
    free(session.session_data.config.model);
}

void test_history_anthropic_wire_claude_model(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    append_tool_turn(&session, API_TYPE_ANTHROPIC, "anthropic/claude-opus-5.5");

    const ConversationMessage* last = &session.session_data.conversation.data[
        session.session_data.conversation.count - 1];
    TEST_ASSERT_NULL_MESSAGE(strstr(last->content, "\"tool_calls\""),
                             "Anthropic wire history must not use OpenAI tool_calls shape");

    cleanup_conversation_history(&session.session_data.conversation);
    free(session.session_data.config.model);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_payload_tools_openai_wire_claude_model);
    RUN_TEST(test_payload_tools_openai_wire_unknown_model);
    RUN_TEST(test_payload_tools_anthropic_wire_claude_model);
    RUN_TEST(test_round_trip_parses_openai_tool_calls_claude_model);
    RUN_TEST(test_history_openai_wire_claude_model);
    RUN_TEST(test_history_openai_wire_unknown_model);
    RUN_TEST(test_history_anthropic_wire_claude_model);

    return UNITY_END();
}
