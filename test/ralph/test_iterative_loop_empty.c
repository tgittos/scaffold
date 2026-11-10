/*
 * Test: iterative_loop empty-response retry
 *
 * A tool-loop round with no tool calls, no visible text and no reasoning must
 * be retried (bounded) instead of ending the turn in silence, and a model that
 * keeps returning nothing must not leave a blank assistant message behind.
 */

#include "unity.h"
#include "agent/session.h"
#include "agent/session_configurator.h"
#include "db/hnswlib_wrapper.h"
#include "mock_api_server.h"
#include "util/app_home.h"
#include "util/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void hnswlib_clear_all(void);

/* Streaming is the default dispatch mode, so the mock always answers SSE. */
static const char SSE_EMPTY[] =
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"\"}}]}\n\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,\"total_tokens\":15}}\n\n"
    "data: [DONE]\n\n";

static const char SSE_FINAL_TEXT[] =
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"All done.\"}}]}\n\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,\"total_tokens\":15}}\n\n"
    "data: [DONE]\n\n";

typedef struct {
    int call_count;
    int max_empty;  /* empty rounds served before the final text */
} CallScript;

static MockAPIServer g_server;
static MockAPIResponse g_response;
static AgentSession g_session;
static int g_server_running;
static int g_session_live;

static char* scripted_callback(const char* request_body, void* user_data) {
    (void)request_body;
    CallScript* script = (CallScript*)user_data;
    int call = ++script->call_count;
    return strdup(call <= script->max_empty ? SSE_EMPTY : SSE_FINAL_TEXT);
}

static void start_session(CallScript* script) {
    memset(&g_server, 0, sizeof(g_server));
    memset(&g_response, 0, sizeof(g_response));
    g_server.port = MOCK_SERVER_DEFAULT_PORT;
    g_response.endpoint = "/v1/chat/completions";
    g_response.method = "POST";
    g_response.response_code = 200;
    g_response.callback = scripted_callback;
    g_response.callback_data = script;
    g_server.responses = &g_response;
    g_server.response_count = 1;

    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&g_server));
    g_server_running = 1;
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&g_server, 1000));

    setenv("API_URL", "http://127.0.0.1:8888/v1/chat/completions", 1);
    setenv("MODEL", "test-model", 1);

    memset(&g_session, 0, sizeof(g_session));
    TEST_ASSERT_EQUAL_INT(0, session_init(&g_session));
    g_session_live = 1;
    TEST_ASSERT_EQUAL_INT(0, session_load_config(&g_session));
    config_set("api_max_retries", "0");
    free(g_session.session_data.config.api_url);
    g_session.session_data.config.api_url = strdup("http://127.0.0.1:8888/v1/chat/completions");
}

static int run_tool_workflow(const char* id) {
    ToolCall call = {
        .id = (char*)id,
        .name = "vector_db",
        .arguments = "{\"operation\":\"list_indices\"}",
    };
    return session_execute_tool_workflow(&g_session, &call, 1, "hello", 200);
}

static const ConversationMessage* last_message(void) {
    const ConversationHistory* h = &g_session.session_data.conversation;
    TEST_ASSERT_TRUE(h->count > 0);
    return &h->data[h->count - 1];
}

void setUp(void) {
    app_home_init(NULL);
    hnswlib_clear_all();
}

/* Cleanup lives here so a failed assertion cannot leak the server port. */
void tearDown(void) {
    if (g_session_live) { session_cleanup(&g_session); g_session_live = 0; }
    if (g_server_running) { mock_api_server_stop(&g_server); g_server_running = 0; }
    unsetenv("API_URL");
    unsetenv("MODEL");
    config_cleanup();
    app_home_cleanup();
}

void test_empty_round_is_retried_and_recovers(void) {
    CallScript script = { .max_empty = 2 };
    start_session(&script);

    TEST_ASSERT_EQUAL_INT(0, run_tool_workflow("empty_recover_1"));

    /* empty, retry, empty, retry, final text */
    TEST_ASSERT_EQUAL_INT(3, script.call_count);
    const ConversationMessage* last = last_message();
    TEST_ASSERT_EQUAL_STRING("assistant", last->role);
    TEST_ASSERT_EQUAL_STRING("All done.", last->content);
}

void test_persistent_empty_round_ends_turn_without_blank_message(void) {
    CallScript script = { .max_empty = 1000 };
    start_session(&script);

    TEST_ASSERT_EQUAL_INT(0, run_tool_workflow("empty_persist_1"));

    /* first attempt + 3 bounded retries */
    TEST_ASSERT_EQUAL_INT(4, script.call_count);
    TEST_ASSERT_EQUAL_STRING("tool", last_message()->role);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_round_is_retried_and_recovers);
    RUN_TEST(test_persistent_empty_round_ends_turn_without_blank_message);
    return UNITY_END();
}
