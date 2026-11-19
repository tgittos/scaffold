#include "unity.h"
#include "agent/session.h"
#include "session/session_manager.h"
#include "session/conversation_tracker.h"
#include "agent/recap.h"
#include "agent/repl.h"
#include "ui/status_line.h"
#include "mock_api_server.h"
#include "util/app_home.h"
#include "llm/llm_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static AgentSession g_session;

static void init_bare_session(AgentSession* s) {
    memset(s, 0, sizeof(*s));
    session_data_init(&s->session_data);
}

static void cleanup_bare_session(AgentSession* s) {
    session_data_cleanup(&s->session_data);
}

void setUp(void) {}
void tearDown(void) {}

void test_recap_null_session(void) {
    int result = session_generate_recap(NULL, 5);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_recap_empty_conversation(void) {
    init_bare_session(&g_session);

    int result = session_generate_recap(&g_session, 5);
    TEST_ASSERT_EQUAL_INT(0, result);

    cleanup_bare_session(&g_session);
}

void test_recap_does_not_persist_conversation(void) {
    init_bare_session(&g_session);

    append_conversation_message(&g_session.session_data.conversation, "user", "Hello");
    append_conversation_message(&g_session.session_data.conversation, "assistant", "Hi there!");
    append_conversation_message(&g_session.session_data.conversation, "user", "How are you?");

    int original_count = g_session.session_data.conversation.count;
    TEST_ASSERT_EQUAL_INT(3, original_count);

    int result = session_generate_recap(&g_session, 5);
    (void)result;

    TEST_ASSERT_EQUAL_INT(original_count, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

void test_recap_max_messages_zero_uses_default(void) {
    init_bare_session(&g_session);

    append_conversation_message(&g_session.session_data.conversation, "user", "Test message");

    int original_count = g_session.session_data.conversation.count;

    int result = session_generate_recap(&g_session, 0);
    (void)result;

    TEST_ASSERT_EQUAL_INT(original_count, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

void test_recap_skips_tool_messages(void) {
    init_bare_session(&g_session);

    append_conversation_message(&g_session.session_data.conversation, "user", "Run a command");
    append_tool_message(&g_session.session_data.conversation, "Command output", "call_123", "shell");
    append_conversation_message(&g_session.session_data.conversation, "assistant", "Here's the result");

    int original_count = g_session.session_data.conversation.count;
    TEST_ASSERT_EQUAL_INT(3, original_count);

    int result = session_generate_recap(&g_session, 5);
    (void)result;

    TEST_ASSERT_EQUAL_INT(original_count, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

void test_greet_null_args(void) {
    init_bare_session(&g_session);

    TEST_ASSERT_EQUAL_INT(-1, recap_greet(NULL, "scaffold"));
    TEST_ASSERT_EQUAL_INT(-1, recap_greet(&g_session, NULL));

    cleanup_bare_session(&g_session);
}

void test_greet_does_not_persist_conversation(void) {
    init_bare_session(&g_session);

    int result = recap_greet(&g_session, "scaffold");
    (void)result;

    TEST_ASSERT_EQUAL_INT(0, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

/* --- greeting tests --- */

static int g_greeting_request_count = 0;

static char* count_greeting_request(const char* request_body, void* user_data) {
    (void)request_body;
    int* count = (int*)user_data;
    (*count)++;
    return NULL;
}

void test_startup_greeting_makes_no_request(void) {
    g_greeting_request_count = 0;

    MockAPIResponse responses[1];
    memset(responses, 0, sizeof(responses));
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    responses[0].response_body = "{}";
    responses[0].response_code = 200;
    responses[0].callback = count_greeting_request;
    responses[0].callback_data = &g_greeting_request_count;

    MockAPIServer server;
    memset(&server, 0, sizeof(server));
    server.port = MOCK_SERVER_DEFAULT_PORT;
    server.responses = responses;
    server.response_count = 1;

    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));

    AgentSession session;
    init_bare_session(&session);
    session.session_data.config.api_url = strdup("http://127.0.0.1:8888/v1/chat/completions");
    session.session_data.config.api_key = strdup("test-key");
    session.session_data.config.model = strdup("gpt-4o");
    /* conversation.count == 0: new session */

    /* Suppress stdout (banner text) during the call */
    int devnull = open("/dev/null", O_WRONLY);
    int saved_out = dup(STDOUT_FILENO);
    dup2(devnull, STDOUT_FILENO);
    close(devnull);

    repl_show_greeting(&session, false, 0, 0);

    fflush(stdout);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);

    mock_api_server_stop(&server);
    cleanup_bare_session(&session);

    TEST_ASSERT_EQUAL_INT(0, g_greeting_request_count);
}

void test_resume_banner_text(void) {
    AgentSession session;
    init_bare_session(&session);

    append_conversation_message(&session.session_data.conversation, "user", "Hello");
    append_conversation_message(&session.session_data.conversation, "assistant", "Hi there!");
    append_conversation_message(&session.session_data.conversation, "user", "How are you?");
    append_conversation_message(&session.session_data.conversation, "assistant", "I'm good!");

    /* Capture stdout */
    int pipefd[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipefd));
    int saved_out = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    time_t fake_now = time(NULL);
    time_t fake_last = fake_now - 7200; /* 2 hours ago */
    repl_show_greeting(&session, false, fake_last, fake_now);

    fflush(stdout);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);

    char buf[512] = {0};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);
    if (n > 0) buf[n] = '\0';

    cleanup_bare_session(&session);

    TEST_ASSERT_TRUE(strstr(buf, "Resumed session: 4 messages") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "/recap for a summary.") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "2h ago") != NULL);
}

static const char* THINKING_SSE_BODY =
    "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\","
    "\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
    "\"reasoning_content\":\"Thinking...\",\"content\":\"\"},"
    "\"finish_reason\":null}]}\n\n"
    "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\","
    "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello\"},"
    "\"finish_reason\":null}]}\n\n"
    "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\","
    "\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}],"
    "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}\n\n"
    "data: [DONE]\n\n";

void test_thinking_clears_busy(void) {
    app_home_init(NULL);
    llm_client_init();

    MockAPIResponse responses[1];
    memset(responses, 0, sizeof(responses));
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    responses[0].response_body = THINKING_SSE_BODY;
    responses[0].response_code = 200;

    MockAPIServer server;
    memset(&server, 0, sizeof(server));
    server.port = MOCK_SERVER_DEFAULT_PORT;
    server.responses = responses;
    server.response_count = 1;

    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));

    AgentSession session;
    init_bare_session(&session);
    append_conversation_message(&session.session_data.conversation, "user", "Hello");
    session.session_data.config.api_url =
        strdup("http://127.0.0.1:8888/v1/chat/completions");
    session.session_data.config.api_key = strdup("test-key");
    session.session_data.config.model = strdup("gpt-4o");
    session.session_data.config.system_prompt = strdup("You are helpful.");
    session.session_data.config.max_tokens_param = "max_tokens";
    session.session_data.config.max_tokens = 300;

    status_line_init();

    /* Suppress streaming output during the call */
    int devnull = open("/dev/null", O_WRONLY);
    int saved_out = dup(STDOUT_FILENO);
    dup2(devnull, STDOUT_FILENO);
    close(devnull);

    int result = session_generate_recap(&session, 5);
    (void)result;

    fflush(stdout);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);

    /* After a thinking callback fires, the status line must be idle */
    TEST_ASSERT_FALSE(status_line_is_busy());

    status_line_cleanup();
    mock_api_server_stop(&server);
    cleanup_bare_session(&session);
    llm_client_cleanup();
    app_home_cleanup();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_recap_null_session);
    RUN_TEST(test_recap_empty_conversation);
    RUN_TEST(test_recap_does_not_persist_conversation);
    RUN_TEST(test_recap_max_messages_zero_uses_default);
    RUN_TEST(test_recap_skips_tool_messages);
    RUN_TEST(test_greet_null_args);
    RUN_TEST(test_greet_does_not_persist_conversation);
    RUN_TEST(test_startup_greeting_makes_no_request);
    RUN_TEST(test_resume_banner_text);
    RUN_TEST(test_thinking_clears_busy);

    return UNITY_END();
}
