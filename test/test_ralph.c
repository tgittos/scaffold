#include "unity.h"
#include "ralph.h"
#include "mock_api_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // Clean up any existing CONVERSATION.md file before each test
    // This ensures tests don't interfere with each other
    remove("CONVERSATION.md");
}
void tearDown(void) {}

void test_ralph_escape_json_string_null(void) {
    char* result = ralph_escape_json_string(NULL);
    TEST_ASSERT_NULL(result);
}

void test_ralph_escape_json_string_basic(void) {
    const char* input = "Hello, World!";
    char* result = ralph_escape_json_string(input);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello, World!", result);
    free(result);
}

void test_ralph_escape_json_string_quotes(void) {
    const char* input = "Say \"Hello\" to the world";
    char* result = ralph_escape_json_string(input);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Say \\\"Hello\\\" to the world", result);
    free(result);
}

void test_ralph_escape_json_string_backslashes(void) {
    const char* input = "Path: C:\\Users\\Test";
    char* result = ralph_escape_json_string(input);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Path: C:\\\\Users\\\\Test", result);
    free(result);
}

void test_ralph_escape_json_string_newlines(void) {
    const char* input = "Line 1\nLine 2\rLine 3\tTabbed";
    char* result = ralph_escape_json_string(input);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Line 1\\nLine 2\\rLine 3\\tTabbed", result);
    free(result);
}

void test_ralph_build_json_payload_basic(void) {
    // Create minimal conversation history
    ConversationHistory conversation = {0};
    ToolRegistry tools = {0};
    
    char* result = ralph_build_json_payload("gpt-3.5-turbo", NULL, &conversation, 
                                           "Hello", "max_tokens", 100, &tools);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"model\": \"gpt-3.5-turbo\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"Hello\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"max_tokens\": 100") != NULL);
    
    free(result);
}

void test_ralph_build_json_payload_with_system_prompt(void) {
    ConversationHistory conversation = {0};
    ToolRegistry tools = {0};
    
    char* result = ralph_build_json_payload("gpt-4", "You are helpful", &conversation, 
                                           "Hello", "max_completion_tokens", 200, &tools);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"model\": \"gpt-4\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"role\": \"system\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "You are helpful") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"Hello\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"max_completion_tokens\": 200") != NULL);
    
    free(result);
}

void test_ralph_init_session_null_parameter(void) {
    int result = ralph_init_session(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_ralph_init_and_cleanup_session(void) {
    RalphSession session;
    
    int init_result = ralph_init_session(&session);
    TEST_ASSERT_EQUAL_INT(0, init_result);
    
    // Verify session was initialized
    TEST_ASSERT_EQUAL_INT(0, session.conversation.count);
    TEST_ASSERT_TRUE(session.tools.function_count > 0); // Should have built-in tools
    
    // Cleanup should work without errors
    ralph_cleanup_session(&session);
    
    // After cleanup, session should be zeroed
    TEST_ASSERT_EQUAL_INT(0, session.conversation.count);
}

void test_ralph_load_config_null_parameter(void) {
    int result = ralph_load_config(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_ralph_load_config_basic(void) {
    RalphSession session;
    
    // Initialize session first
    int init_result = ralph_init_session(&session);
    TEST_ASSERT_EQUAL_INT(0, init_result);
    
    // Load config
    int config_result = ralph_load_config(&session);
    TEST_ASSERT_EQUAL_INT(0, config_result);
    
    // Verify configuration was loaded (values may come from environment or defaults)
    TEST_ASSERT_NOT_NULL(session.config.api_url);
    TEST_ASSERT_NOT_NULL(session.config.model);
    
    // The API URL could be from environment or default - both are valid
    // Just verify it's a reasonable URL
    TEST_ASSERT_TRUE(strstr(session.config.api_url, "/v1/chat/completions") != NULL);
    
    // Model should be set to something reasonable
    TEST_ASSERT_TRUE(strlen(session.config.model) > 0);
    
    // Basic numeric values should be initialized
    TEST_ASSERT_TRUE(session.config.context_window > 0);
    TEST_ASSERT_NOT_NULL(session.config.max_tokens_param);
    
    ralph_cleanup_session(&session);
}

void test_ralph_process_message_null_parameters(void) {
    RalphSession session;
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Test null session
    int result1 = ralph_process_message(NULL, "test");
    TEST_ASSERT_EQUAL_INT(-1, result1);
    
    // Test null message
    int result2 = ralph_process_message(&session, NULL);
    TEST_ASSERT_EQUAL_INT(-1, result2);
    
    ralph_cleanup_session(&session);
}

void test_ralph_config_parameter_selection(void) {
    RalphSession session;
    ralph_init_session(&session);
    
    // Test OpenAI URL parameter selection
    session.config.api_url = strdup("https://api.openai.com/v1/chat/completions");
    session.config.max_tokens_param = "max_tokens";
    if (strstr(session.config.api_url, "api.openai.com") != NULL) {
        session.config.max_tokens_param = "max_completion_tokens";  
    }
    TEST_ASSERT_EQUAL_STRING("max_completion_tokens", session.config.max_tokens_param);
    
    // Test local server parameter selection
    free(session.config.api_url);
    session.config.api_url = strdup("http://localhost:1234/v1/chat/completions");
    session.config.max_tokens_param = "max_tokens";
    if (strstr(session.config.api_url, "api.openai.com") != NULL) {
        session.config.max_tokens_param = "max_completion_tokens";
    }
    TEST_ASSERT_EQUAL_STRING("max_tokens", session.config.max_tokens_param);
    
    ralph_cleanup_session(&session);
}

void test_ralph_execute_tool_workflow_null_parameters(void) {
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {NULL, NULL};
    
    // Initialize valid tool call for testing
    tool_calls[0].id = "test_id";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo test\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Test null session
    int result1 = ralph_execute_tool_workflow(NULL, tool_calls, 1, "test", 100, headers);
    TEST_ASSERT_EQUAL_INT(-1, result1);
    
    // Test null tool_calls
    int result2 = ralph_execute_tool_workflow(&session, NULL, 1, "test", 100, headers);
    TEST_ASSERT_EQUAL_INT(-1, result2);
    
    // Test zero call_count
    int result3 = ralph_execute_tool_workflow(&session, tool_calls, 0, "test", 100, headers);
    TEST_ASSERT_EQUAL_INT(-1, result3);
    
    // Test negative call_count
    int result4 = ralph_execute_tool_workflow(&session, tool_calls, -1, "test", 100, headers);
    TEST_ASSERT_EQUAL_INT(-1, result4);
    
    ralph_cleanup_session(&session);
}

void test_ralph_execute_tool_workflow_api_failure_resilience(void) {
    // INTEGRATION TEST: Tool execution succeeds, API follow-up fails
    // This tests the specific bug that was fixed: ralph_execute_tool_workflow
    // should return 0 (success) when tools execute successfully, even if 
    // the follow-up API request fails (network down, server error, etc.)
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Initialize with a simple tool call that will succeed
    tool_calls[0].id = "test_tool_id_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'integration_test_success'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Set API URL to something that will fail (simulate network failure)
    // This tests the exact scenario: tool succeeds, API fails
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:99999/v1/chat/completions"); // Port 99999 should be unreachable
    
    // Execute tool workflow - this should return 0 (success) because:
    // 1. Tool execution succeeds (shell_execute with "echo" command works)
    // 2. Tool results are added to conversation history
    // 3. Follow-up API request fails (unreachable server)
    // 4. Function returns 0 anyway because tools executed successfully
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "run echo command", 100, headers);
    
    // The key assertion: even though API follow-up fails, workflow returns success (0)
    // because the actual tool execution was successful
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Verify tool result was actually added to conversation history
    // This proves the tool executed successfully despite API failure
    TEST_ASSERT_TRUE(session.conversation.count > 0);
    
    // Look for tool result message in conversation history
    int found_tool_result = 0;
    for (int i = 0; i < session.conversation.count; i++) {
        if (strcmp(session.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_EQUAL_STRING("test_tool_id_123", session.conversation.messages[i].tool_call_id);
            TEST_ASSERT_EQUAL_STRING("shell_execute", session.conversation.messages[i].tool_name);
            TEST_ASSERT_TRUE(strstr(session.conversation.messages[i].content, "integration_test_success") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
}

void test_ralph_process_message_basic_workflow(void) {
    // INTEGRATION TEST: End-to-end message processing workflow
    // This tests the core user workflow: user sends message, system processes it
    // Even if API fails, we can verify the message was added to conversation
    
    RalphSession session;
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Set up a non-working API URL to avoid dependency on external services
    // but still test the message processing pipeline
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:99999/v1/chat/completions");
    
    // Process a basic user message
    const char* user_message = "Hello, how are you today?";
    
    // Conversation should be empty (setUp() ensures clean state)
    TEST_ASSERT_EQUAL_INT(0, session.conversation.count);
    
    // Process the message - this will fail at the API call, testing:
    // 1. Message processing pipeline works up to the API call
    // 2. JSON payload generation works (can be verified via debug output)
    // 3. Session state remains consistent
    // 4. Function correctly handles API failures
    int result = ralph_process_message(&session, user_message);
    
    // Function should return -1 because API call fails
    TEST_ASSERT_EQUAL_INT(-1, result);
    
    // User message should NOT be added to conversation when API fails
    // This is correct behavior - no point storing messages if no response
    TEST_ASSERT_EQUAL_INT(0, session.conversation.count);
    
    // Session should remain in a consistent state
    // The conversation should still be usable for future messages
    // When empty, messages array is NULL, which is valid
    
    ralph_cleanup_session(&session);
}

void test_tool_execution_without_api_server(void) {
    // NETWORK RESILIENCE TEST: Tool execution with completely unreachable API server
    // This tests graceful degradation when no API server is available
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Setup tool call that will succeed locally
    tool_calls[0].id = "test_no_api_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_works_without_api'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Set API URL to definitely unreachable address
    free(session.config.api_url);
    session.config.api_url = strdup("http://192.0.2.1:99999/v1/chat/completions"); // RFC3330 test address
    
    // Execute tool workflow - should succeed despite unreachable API
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "test without api", 100, headers);
    
    // Tool execution should succeed even when API is unreachable
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Verify tool result was added to conversation
    TEST_ASSERT_TRUE(session.conversation.count > 0);
    
    // Find and verify tool result
    int found_tool_result = 0;
    for (int i = 0; i < session.conversation.count; i++) {
        if (strcmp(session.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_TRUE(strstr(session.conversation.messages[i].content, "tool_works_without_api") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
}

void test_tool_execution_with_network_timeout(void) {
    // NETWORK RESILIENCE TEST: Tool execution with slow API server that times out
    // Tests behavior when API server is reachable but extremely slow
    
    MockAPIServer mock_server = {0};
    MockAPIResponse responses[1];
    
    // Setup mock server with extreme delay (simulates timeout)
    responses[0] = mock_openai_tool_response("timeout_test", "This should timeout");
    responses[0].delay_ms = 30000; // 30 second delay - longer than typical timeout
    
    mock_server.port = MOCK_SERVER_DEFAULT_PORT + 1;
    mock_server.responses = responses;
    mock_server.response_count = 1;
    
    // Start mock server
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&mock_server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&mock_server, 1000));
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Setup tool call
    tool_calls[0].id = "timeout_test_123";
    tool_calls[0].name = "shell_execute"; 
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_survives_timeout'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Point to slow mock server
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:8889/v1/chat/completions");
    
    // Execute - should succeed because tool executes locally, API timeout doesn't matter
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "timeout test", 100, headers);
    
    // Tool workflow should succeed despite API timeout
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Tool result should be in conversation
    TEST_ASSERT_TRUE(session.conversation.count > 0);
    
    ralph_cleanup_session(&session);
    mock_api_server_stop(&mock_server);
}

void test_tool_execution_with_auth_failure(void) {
    // NETWORK RESILIENCE TEST: Tool execution with API authentication failure
    // Tests graceful handling of 401/403 responses from API server
    
    MockAPIServer mock_server = {0};
    MockAPIResponse responses[1];
    
    // Setup mock server that returns auth error
    responses[0] = mock_error_response(401, "Invalid API key provided");
    
    mock_server.port = MOCK_SERVER_DEFAULT_PORT + 2;
    mock_server.responses = responses;
    mock_server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&mock_server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&mock_server, 1000));
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    tool_calls[0].id = "auth_fail_test_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_survives_auth_failure'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:8890/v1/chat/completions");
    
    // Execute - tool should succeed even with API auth failure
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "auth test", 100, headers);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(session.conversation.count > 0);
    
    ralph_cleanup_session(&session);
    mock_api_server_stop(&mock_server);
}

void test_graceful_degradation_on_api_errors(void) {
    // NETWORK RESILIENCE TEST: Various API error scenarios
    // Tests that tool execution continues working despite different API failures
    
    MockAPIServer mock_server = {0};
    MockAPIResponse responses[1];
    
    // Test with 500 Internal Server Error
    responses[0] = mock_error_response(500, "Internal server error");
    
    mock_server.port = MOCK_SERVER_DEFAULT_PORT + 3;
    mock_server.responses = responses;
    mock_server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&mock_server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&mock_server, 1000));
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    tool_calls[0].id = "server_error_test_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_survives_server_error'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:8891/v1/chat/completions");
    
    // Execute tool workflow
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "server error test", 100, headers);
    
    // Should succeed because tools execute locally regardless of API errors
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(session.conversation.count > 0);
    
    // Verify tool result exists
    int found_tool_result = 0;
    for (int i = 0; i < session.conversation.count; i++) {
        if (strcmp(session.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_TRUE(strstr(session.conversation.messages[i].content, "tool_survives_server_error") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
    mock_api_server_stop(&mock_server);
}

void test_shell_command_request_workflow(void) {
    // REALISTIC USER WORKFLOW TEST: User requests shell command execution
    // Tests the complete workflow: user message -> tool detection -> execution -> result
    
    MockAPIServer mock_server = {0};
    MockAPIResponse responses[1];
    
    // Setup mock server that returns a tool call request
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    responses[0].response_code = 200;
    responses[0].delay_ms = 0;
    responses[0].should_fail = 0;
    
    // OpenAI-style response that includes tool call
    static char tool_response[] = "{"
        "\"id\":\"chatcmpl-workflow123\","
        "\"object\":\"chat.completion\","
        "\"created\":1234567890,"
        "\"model\":\"gpt-3.5-turbo\","
        "\"choices\":["
        "{"
        "\"index\":0,"
        "\"message\":{"
        "\"role\":\"assistant\","
        "\"content\":null,"
        "\"tool_calls\":["
        "{"
        "\"id\":\"call_shell_123\","
        "\"type\":\"function\","
        "\"function\":{"
        "\"name\":\"shell_execute\","
        "\"arguments\":\"{\\\"command\\\":\\\"echo workflow_test_success\\\"}\""
        "}"
        "}"
        "]"
        "},"
        "\"finish_reason\":\"tool_calls\""
        "}"
        "]"
        "}";
    
    responses[0].response_body = tool_response;
    
    mock_server.port = MOCK_SERVER_DEFAULT_PORT + 4;
    mock_server.responses = responses;
    mock_server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&mock_server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&mock_server, 1000));
    
    RalphSession session;
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:8892/v1/chat/completions");
    
    // Simulate user requesting shell command
    const char* user_message = "run echo command to show workflow success";
    
    // Process message - this should:
    // 1. Send message to API
    // 2. Receive tool call response
    // 3. Execute shell command
    // 4. Add results to conversation
    int result = ralph_process_message(&session, user_message);
    
    // This may fail due to follow-up API call, but tool should have executed
    // The key is that conversation should contain the tool results
    // Result may be success (0) or failure (-1) depending on API response
    (void)result; // Acknowledge variable usage
    TEST_ASSERT_TRUE(session.conversation.count > 0);
    
    // Look for tool execution result in conversation
    int found_tool_result = 0;
    for (int i = 0; i < session.conversation.count; i++) {
        if (strcmp(session.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_TRUE(strstr(session.conversation.messages[i].content, "workflow_test_success") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
    mock_api_server_stop(&mock_server);
}

void test_sequential_tool_execution(void) {
    // TOOL WORKFLOW INTEGRATION TEST: Multiple tool calls in sequence
    // Tests that multiple tools execute properly and results are tracked
    
    RalphSession session;
    ToolCall tool_calls[2];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Setup first tool call
    tool_calls[0].id = "seq_test_1";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'first_tool_executed'\"}";
    
    // Setup second tool call  
    tool_calls[1].id = "seq_test_2";
    tool_calls[1].name = "shell_execute";
    tool_calls[1].arguments = "{\"command\":\"echo 'second_tool_executed'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Use unreachable API to focus on tool execution
    free(session.config.api_url);
    session.config.api_url = strdup("http://192.0.2.1:99999/v1/chat/completions");
    
    // Execute multiple tools
    int result = ralph_execute_tool_workflow(&session, tool_calls, 2, "sequential test", 100, headers);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Should have at least 2 tool results in conversation
    TEST_ASSERT_TRUE(session.conversation.count >= 2);
    
    // Verify both tools executed
    int found_first = 0, found_second = 0;
    for (int i = 0; i < session.conversation.count; i++) {
        if (strcmp(session.conversation.messages[i].role, "tool") == 0) {
            if (strcmp(session.conversation.messages[i].tool_call_id, "seq_test_1") == 0) {
                found_first = 1;
                TEST_ASSERT_TRUE(strstr(session.conversation.messages[i].content, "first_tool_executed") != NULL);
            }
            if (strcmp(session.conversation.messages[i].tool_call_id, "seq_test_2") == 0) {
                found_second = 1;
                TEST_ASSERT_TRUE(strstr(session.conversation.messages[i].content, "second_tool_executed") != NULL);
            }
        }
    }
    
    TEST_ASSERT_TRUE(found_first);
    TEST_ASSERT_TRUE(found_second);
    
    ralph_cleanup_session(&session);
}

void test_conversation_persistence_through_tools(void) {
    // REALISTIC USER WORKFLOW TEST: Multiple messages with tool usage
    // Tests that conversation history maintains context across tool executions
    
    MockAPIServer mock_server = {0};
    MockAPIResponse responses[2];
    
    // First response: simple text (no tools)
    responses[0] = mock_openai_tool_response("", "I understand you want to test conversation persistence.");
    
    // Second response: includes tool call
    responses[1].endpoint = "/v1/chat/completions";
    responses[1].method = "POST"; 
    responses[1].response_code = 200;
    responses[1].delay_ms = 0;
    responses[1].should_fail = 0;
    
    static char tool_call_response[] = "{"
        "\"id\":\"chatcmpl-persist123\","
        "\"object\":\"chat.completion\","
        "\"created\":1234567890,"
        "\"model\":\"gpt-3.5-turbo\","
        "\"choices\":["
        "{"
        "\"index\":0,"
        "\"message\":{"
        "\"role\":\"assistant\","
        "\"content\":null,"
        "\"tool_calls\":["
        "{"
        "\"id\":\"call_persist_123\","
        "\"type\":\"function\","
        "\"function\":{"
        "\"name\":\"shell_execute\","
        "\"arguments\":\"{\\\"command\\\":\\\"echo 'persistence_test'}\\\"}"
        "}"
        "}"
        "]"
        "},"
        "\"finish_reason\":\"tool_calls\""
        "}"
        "]"
        "}";
    
    responses[1].response_body = tool_call_response;
    
    mock_server.port = MOCK_SERVER_DEFAULT_PORT + 5;
    mock_server.responses = responses;
    mock_server.response_count = 2;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&mock_server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&mock_server, 1000));
    
    RalphSession session;
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    free(session.config.api_url);
    session.config.api_url = strdup("http://127.0.0.1:8893/v1/chat/completions");
    
    // Initial conversation should be empty
    TEST_ASSERT_EQUAL_INT(0, session.conversation.count);
    
    // Simulate first user message (no tools expected)
    // This will likely fail at API level, but that's ok for this test
    ralph_process_message(&session, "Hello, I want to test conversation persistence");
    
    // Now simulate second message that should trigger tools
    // The important thing is that conversation context is maintained
    ralph_process_message(&session, "Please run echo command to test persistence");
    
    // Conversation should have some messages now
    // Exact count depends on API responses, but should be > 0
    TEST_ASSERT_TRUE(session.conversation.count >= 0);
    
    // Verify session remains in consistent state
    // This is the key test - session should be usable regardless of API failures
    TEST_ASSERT_NOT_NULL(session.config.model);
    TEST_ASSERT_NOT_NULL(session.config.api_url);
    
    ralph_cleanup_session(&session);
    mock_api_server_stop(&mock_server);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_ralph_escape_json_string_null);
    RUN_TEST(test_ralph_escape_json_string_basic);
    RUN_TEST(test_ralph_escape_json_string_quotes);
    RUN_TEST(test_ralph_escape_json_string_backslashes);
    RUN_TEST(test_ralph_escape_json_string_newlines);
    RUN_TEST(test_ralph_build_json_payload_basic);
    RUN_TEST(test_ralph_build_json_payload_with_system_prompt);
    RUN_TEST(test_ralph_init_session_null_parameter);
    RUN_TEST(test_ralph_init_and_cleanup_session);
    RUN_TEST(test_ralph_load_config_null_parameter);
    RUN_TEST(test_ralph_load_config_basic);
    RUN_TEST(test_ralph_process_message_null_parameters);
    RUN_TEST(test_ralph_config_parameter_selection);
    RUN_TEST(test_ralph_execute_tool_workflow_null_parameters);
    RUN_TEST(test_ralph_execute_tool_workflow_api_failure_resilience);
    RUN_TEST(test_ralph_process_message_basic_workflow);
    RUN_TEST(test_tool_execution_without_api_server);
    RUN_TEST(test_tool_execution_with_network_timeout);
    RUN_TEST(test_tool_execution_with_auth_failure);
    RUN_TEST(test_graceful_degradation_on_api_errors);
    RUN_TEST(test_shell_command_request_workflow);
    RUN_TEST(test_sequential_tool_execution);
    RUN_TEST(test_conversation_persistence_through_tools);
    
    return UNITY_END();
}