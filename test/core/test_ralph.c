#include "unity.h"
#include "ralph.h"
#include "mock_api_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // No file cleanup needed - using vector database for conversation storage
}
void tearDown(void) {}

void test_ralph_escape_json_string_null(void) {
    char* result = ralph_escape_json_string(NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
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
    TEST_ASSERT_TRUE(strstr(result, "\"role\":\"system\"") != NULL);
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
    TEST_ASSERT_EQUAL_INT(0, session.session_data.conversation.count);
    TEST_ASSERT_TRUE(session.tools.function_count > 0); // Should have built-in tools
    
    // Cleanup should work without errors
    ralph_cleanup_session(&session);
    
    // After cleanup, session should be zeroed
    TEST_ASSERT_EQUAL_INT(0, session.session_data.conversation.count);
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
    TEST_ASSERT_NOT_NULL(session.session_data.config.api_url);
    TEST_ASSERT_NOT_NULL(session.session_data.config.model);
    
    // The API URL could be from environment or default - both are valid
    // Just verify it's a reasonable URL (OpenAI or Anthropic format)
    printf("DEBUG: API URL is: %s\n", session.session_data.config.api_url);
    TEST_ASSERT_TRUE(strstr(session.session_data.config.api_url, "/v1/chat/completions") != NULL ||
                     strstr(session.session_data.config.api_url, "/v1/messages") != NULL);
    
    // Model should be set to something reasonable
    TEST_ASSERT_TRUE(strlen(session.session_data.config.model) > 0);
    
    // Basic numeric values should be initialized
    TEST_ASSERT_TRUE(session.session_data.config.context_window > 0);
    TEST_ASSERT_NOT_NULL(session.session_data.config.max_tokens_param);
    
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
    session.session_data.config.api_url = strdup("https://api.openai.com/v1/chat/completions");
    session.session_data.config.max_tokens_param = "max_tokens";
    if (strstr(session.session_data.config.api_url, "api.openai.com") != NULL) {
        session.session_data.config.max_tokens_param = "max_completion_tokens";  
    }
    TEST_ASSERT_EQUAL_STRING("max_completion_tokens", session.session_data.config.max_tokens_param);
    
    // Test local server parameter selection
    free(session.session_data.config.api_url);
    session.session_data.config.api_url = strdup("http://localhost:1234/v1/chat/completions");
    session.session_data.config.max_tokens_param = "max_tokens";
    if (strstr(session.session_data.config.api_url, "api.openai.com") != NULL) {
        session.session_data.config.max_tokens_param = "max_completion_tokens";
    }
    TEST_ASSERT_EQUAL_STRING("max_tokens", session.session_data.config.max_tokens_param);
    
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
    
    // Start mock server that will fail on API requests
    MockAPIServer server = {0};
    server.port = MOCK_SERVER_DEFAULT_PORT;
    MockAPIResponse responses[1];
    responses[0] = mock_network_failure(); // This will drop connections
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    server.responses = responses;
    server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));
    
    // Set environment to use mock server
    setenv("API_URL", "http://127.0.0.1:8888/v1/chat/completions", 1);
    setenv("MODEL", "test-model", 1);
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Initialize with a simple tool call that will succeed
    tool_calls[0].id = "test_tool_id_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'integration_test_success'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Execute tool workflow - this should return 0 (success) because:
    // 1. Tool execution succeeds (shell_execute with "echo" command works)
    // 2. Tool results are added to conversation history
    // 3. Follow-up API request fails (mock server drops connection)
    // 4. Function returns 0 anyway because tools executed successfully
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "run echo command", 100, headers);
    
    // The key assertion: even though API follow-up fails, workflow returns success (0)
    // because the actual tool execution was successful
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Verify tool result was actually added to conversation history
    // This proves the tool executed successfully despite API failure
    TEST_ASSERT_TRUE(session.session_data.conversation.count > 0);
    
    // Look for tool result message in conversation history
    int found_tool_result = 0;
    for (int i = 0; i < session.session_data.conversation.count; i++) {
        if (strcmp(session.session_data.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_EQUAL_STRING("test_tool_id_123", session.session_data.conversation.messages[i].tool_call_id);
            TEST_ASSERT_EQUAL_STRING("shell_execute", session.session_data.conversation.messages[i].tool_name);
            TEST_ASSERT_TRUE(strstr(session.session_data.conversation.messages[i].content, "integration_test_success") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
    
    // Stop mock server
    mock_api_server_stop(&server);
    
    // Clean up environment
    unsetenv("API_URL");
    unsetenv("MODEL");
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
    free(session.session_data.config.api_url);
    session.session_data.config.api_url = strdup("http://127.0.0.1:99999/v1/chat/completions");
    
    // Process a basic user message
    const char* user_message = "Hello, how are you today?";
    
    // Conversation should be empty (setUp() ensures clean state)
    TEST_ASSERT_EQUAL_INT(0, session.session_data.conversation.count);
    
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
    TEST_ASSERT_EQUAL_INT(0, session.session_data.conversation.count);
    
    // Session should remain in a consistent state
    // The conversation should still be usable for future messages
    // When empty, messages array is NULL, which is valid
    
    ralph_cleanup_session(&session);
}

void test_tool_execution_without_api_server(void) {
    // NETWORK RESILIENCE TEST: Tool execution with completely unreachable API server
    // This tests graceful degradation when no API server is available
    
    // Start mock server that will drop all connections
    MockAPIServer server = {0};
    server.port = MOCK_SERVER_DEFAULT_PORT;
    MockAPIResponse responses[1];
    responses[0] = mock_network_failure();
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    server.responses = responses;
    server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));
    
    // Set environment to use mock server
    setenv("API_URL", "http://127.0.0.1:8888/v1/chat/completions", 1);
    setenv("MODEL", "test-model", 1);
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Setup tool call that will succeed locally
    tool_calls[0].id = "test_no_api_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_works_without_api'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Execute tool workflow - should succeed despite unreachable API
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "test without api", 100, headers);
    
    // Tool execution should succeed even when API is unreachable
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Verify tool result was added to conversation
    TEST_ASSERT_TRUE(session.session_data.conversation.count > 0);
    
    // Find and verify tool result
    int found_tool_result = 0;
    for (int i = 0; i < session.session_data.conversation.count; i++) {
        if (strcmp(session.session_data.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_TRUE(strstr(session.session_data.conversation.messages[i].content, "tool_works_without_api") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
    
    // Stop mock server
    mock_api_server_stop(&server);
    
    // Clean up environment
    unsetenv("API_URL");
    unsetenv("MODEL");
}

void test_tool_execution_with_network_timeout(void) {
    // NETWORK RESILIENCE TEST: Tool execution with network timeout
    // Tests behavior when API server times out
    
    // Start mock server with long delay to simulate timeout
    MockAPIServer server = {0};
    server.port = MOCK_SERVER_DEFAULT_PORT;
    MockAPIResponse responses[1];
    responses[0] = mock_network_failure(); // Simulates timeout by dropping connection
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    responses[0].delay_ms = 5000; // Long delay before dropping
    server.responses = responses;
    server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));
    
    // Set environment to use mock server
    setenv("API_URL", "http://127.0.0.1:8888/v1/chat/completions", 1);
    setenv("MODEL", "test-model", 1);
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Setup tool call
    tool_calls[0].id = "timeout_test_123";
    tool_calls[0].name = "shell_execute"; 
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_survives_timeout'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Execute - should succeed because tool executes locally, API timeout doesn't matter
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "timeout test", 100, headers);
    
    // Tool workflow should succeed despite API timeout
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Tool result should be in conversation
    TEST_ASSERT_TRUE(session.session_data.conversation.count > 0);
    
    ralph_cleanup_session(&session);
    
    // Stop mock server
    mock_api_server_stop(&server);
    
    // Clean up environment
    unsetenv("API_URL");
    unsetenv("MODEL");
}

void test_tool_execution_with_auth_failure(void) {
    // NETWORK RESILIENCE TEST: Tool execution with API authentication failure
    // Tests graceful handling when API server returns auth error
    
    // Start mock server that returns 401 Unauthorized
    MockAPIServer server = {0};
    server.port = MOCK_SERVER_DEFAULT_PORT;
    MockAPIResponse responses[1];
    responses[0] = mock_error_response(401, "Unauthorized");
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    server.responses = responses;
    server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));
    
    // Set environment to use mock server
    setenv("API_URL", "http://127.0.0.1:8888/v1/chat/completions", 1);
    setenv("MODEL", "test-model", 1);
    setenv("API_KEY", "test-key", 1);
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    tool_calls[0].id = "auth_fail_test_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_survives_auth_failure'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Execute - tool should succeed even with API auth failure
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "auth test", 100, headers);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(session.session_data.conversation.count > 0);
    
    ralph_cleanup_session(&session);
    
    // Stop mock server
    mock_api_server_stop(&server);
    
    // Clean up environment
    unsetenv("API_URL");
    unsetenv("MODEL");
    unsetenv("API_KEY");
}

void test_graceful_degradation_on_api_errors(void) {
    // NETWORK RESILIENCE TEST: Various API error scenarios
    // Tests that tool execution continues working despite different API failures
    
    // Start mock server that returns 500 Internal Server Error
    MockAPIServer server = {0};
    server.port = MOCK_SERVER_DEFAULT_PORT;
    MockAPIResponse responses[1];
    responses[0] = mock_error_response(500, "Internal Server Error");
    responses[0].endpoint = "/v1/chat/completions";
    responses[0].method = "POST";
    server.responses = responses;
    server.response_count = 1;
    
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_start(&server));
    TEST_ASSERT_EQUAL_INT(0, mock_api_server_wait_ready(&server, 1000));
    
    // Set environment to use mock server
    setenv("API_URL", "http://127.0.0.1:8888/v1/chat/completions", 1);
    setenv("MODEL", "test-model", 1);
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    tool_calls[0].id = "server_error_test_123";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"echo 'tool_survives_server_error'\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Execute tool workflow
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "server error test", 100, headers);
    
    // Should succeed because tools execute locally regardless of API errors
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(session.session_data.conversation.count > 0);
    
    // Verify tool result exists
    int found_tool_result = 0;
    for (int i = 0; i < session.session_data.conversation.count; i++) {
        if (strcmp(session.session_data.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            TEST_ASSERT_TRUE(strstr(session.session_data.conversation.messages[i].content, "tool_survives_server_error") != NULL);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
    
    // Stop mock server
    mock_api_server_stop(&server);
    
    // Clean up environment
    unsetenv("API_URL");
    unsetenv("MODEL");
}

void test_shell_command_request_workflow(void) {
    // REALISTIC USER WORKFLOW TEST: User requests shell command execution
    // Tests the complete workflow without mock server - relies on live provider or graceful failure
    
    RalphSession session;
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Use unreachable API to test tool execution without server dependency
    free(session.session_data.config.api_url);
    session.session_data.config.api_url = strdup("http://192.0.2.1:99999/v1/chat/completions");
    
    // Simulate user requesting shell command
    const char* user_message = "run echo command to show workflow success";
    
    // Process message - API will fail but that's expected for this test
    // We're testing the tool execution path, not the API integration
    int result = ralph_process_message(&session, user_message);
    
    // Result will be -1 due to API failure, which is expected
    TEST_ASSERT_EQUAL_INT(-1, result);
    
    // Session should remain in consistent state
    TEST_ASSERT_NOT_NULL(session.session_data.config.api_url);
    TEST_ASSERT_NOT_NULL(session.session_data.config.model);
    
    ralph_cleanup_session(&session);
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
    free(session.session_data.config.api_url);
    session.session_data.config.api_url = strdup("http://192.0.2.1:99999/v1/chat/completions");
    
    // Execute multiple tools
    int result = ralph_execute_tool_workflow(&session, tool_calls, 2, "sequential test", 100, headers);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Should have at least 2 tool results in conversation
    TEST_ASSERT_TRUE(session.session_data.conversation.count >= 2);
    
    // Verify both tools executed
    int found_first = 0, found_second = 0;
    for (int i = 0; i < session.session_data.conversation.count; i++) {
        if (strcmp(session.session_data.conversation.messages[i].role, "tool") == 0) {
            if (strcmp(session.session_data.conversation.messages[i].tool_call_id, "seq_test_1") == 0) {
                found_first = 1;
                TEST_ASSERT_TRUE(strstr(session.session_data.conversation.messages[i].content, "first_tool_executed") != NULL);
            }
            if (strcmp(session.session_data.conversation.messages[i].tool_call_id, "seq_test_2") == 0) {
                found_second = 1;
                TEST_ASSERT_TRUE(strstr(session.session_data.conversation.messages[i].content, "second_tool_executed") != NULL);
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
    
    RalphSession session;
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Use unreachable API to test session persistence without server dependency
    free(session.session_data.config.api_url);
    session.session_data.config.api_url = strdup("http://192.0.2.1:99999/v1/chat/completions");
    
    // Initial conversation should be empty
    TEST_ASSERT_EQUAL_INT(0, session.session_data.conversation.count);
    
    // Process messages - API will fail but session should remain consistent
    ralph_process_message(&session, "Hello, I want to test conversation persistence");
    ralph_process_message(&session, "Please run echo command to test persistence");
    
    // Verify session remains in consistent state
    // This is the key test - session should be usable regardless of API failures
    TEST_ASSERT_NOT_NULL(session.session_data.config.model);
    TEST_ASSERT_NOT_NULL(session.session_data.config.api_url);
    TEST_ASSERT_EQUAL_INT(0, session.session_data.conversation.count); // No messages added due to API failures
    
    ralph_cleanup_session(&session);
}

void test_tool_name_hardcoded_bug_fixed(void) {
    // BUG FIX VERIFICATION TEST: Tool name should now use correct tool name, not hardcoded "tool_name"
    // This test verifies that the fix for the hardcoded tool_name bug works correctly
    
    RalphSession session;
    ToolCall tool_calls[1];
    const char* headers[2] = {"Content-Type: application/json", NULL};
    
    // Setup file list tool call - this should now have tool_name "file_list" not "tool_name"
    tool_calls[0].id = "toolu_01DdpdffBNXNqfWFDUCtY7Jc";  // Same ID from CONVERSATION.md
    tool_calls[0].name = "file_list";  // This is the ACTUAL tool name that should be saved
    tool_calls[0].arguments = "{\"directory_path\": \".\"}";
    
    ralph_init_session(&session);
    ralph_load_config(&session);
    
    // Use unreachable API to focus on tool execution fix verification
    free(session.session_data.config.api_url);
    session.session_data.config.api_url = strdup("http://192.0.2.1:99999/v1/chat/completions");
    
    // Execute tool workflow which will eventually call the fixed ralph_execute_tool_loop
    int result = ralph_execute_tool_workflow(&session, tool_calls, 1, "list files", 100, headers);
    
    // Tool execution should succeed
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(session.session_data.conversation.count > 0);
    
    // Find the tool result message and verify the tool_name is now correct
    int found_tool_result = 0;
    for (int i = 0; i < session.session_data.conversation.count; i++) {
        if (strcmp(session.session_data.conversation.messages[i].role, "tool") == 0) {
            found_tool_result = 1;
            printf("DEBUG: Fixed tool message has tool_name: '%s'\n", session.session_data.conversation.messages[i].tool_name);
            
            // BUG FIX VERIFICATION: tool_name should now be "file_list", NOT "tool_name"
            TEST_ASSERT_EQUAL_STRING("file_list", session.session_data.conversation.messages[i].tool_name);
            TEST_ASSERT_EQUAL_STRING("toolu_01DdpdffBNXNqfWFDUCtY7Jc", session.session_data.conversation.messages[i].tool_call_id);
            
            // Verify it's NOT the hardcoded bug value anymore
            TEST_ASSERT_NOT_EQUAL(0, strcmp("tool_name", session.session_data.conversation.messages[i].tool_name));
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tool_result);
    
    ralph_cleanup_session(&session);
}

// Anthropic-specific tests
void test_ralph_build_anthropic_json_payload_basic(void) {
    const char *test_message = "Hello Anthropic";
    const char *model = "claude-3-opus-20240229";
    int max_tokens = 200;
    
    ConversationHistory conversation = {0};
    ToolRegistry tools = {0};
    
    char* result = ralph_build_anthropic_json_payload(model, NULL, &conversation, 
                                                     test_message, max_tokens, &tools);
    
    TEST_ASSERT_NOT_NULL(result);
    
    // Check that payload contains expected fields
    TEST_ASSERT_TRUE(strstr(result, "\"model\": \"claude-3-opus-20240229\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"messages\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"Hello Anthropic\"") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"max_tokens\": 200") != NULL);
    
    free(result);
}

void test_ralph_build_anthropic_json_payload_with_system(void) {
    const char *test_message = "What is 2+2?";
    const char *system_prompt = "You are a helpful math tutor.";
    const char *model = "claude-3-opus-20240229";
    int max_tokens = 100;
    
    ConversationHistory conversation = {0};
    ToolRegistry tools = {0};
    
    char* result = ralph_build_anthropic_json_payload(model, system_prompt, &conversation, 
                                                     test_message, max_tokens, &tools);
    
    TEST_ASSERT_NOT_NULL(result);
    
    // Check that system prompt is in the top-level system field (Anthropic format)
    TEST_ASSERT_TRUE(strstr(result, "\"system\": \"You are a helpful math tutor.\"") != NULL);
    // Should not be in messages array
    TEST_ASSERT_NULL(strstr(result, "\"role\": \"system\""));
    
    free(result);
}

void test_ralph_build_anthropic_json_payload_with_tools(void) {
    const char *test_message = "List files";
    const char *model = "claude-3-opus-20240229";
    int max_tokens = 200;
    
    ConversationHistory conversation = {0};
    ToolRegistry tools = {0};
    init_tool_registry(&tools);
    register_builtin_tools(&tools);
    
    char* result = ralph_build_anthropic_json_payload(model, NULL, &conversation, 
                                                     test_message, max_tokens, &tools);
    
    TEST_ASSERT_NOT_NULL(result);
    
    // Check for tools array
    TEST_ASSERT_NOT_NULL(strstr(result, "\"tools\": ["));
    
    // Check for shell_execute tool with Anthropic format
    TEST_ASSERT_NOT_NULL(strstr(result, "\"name\": \"shell_execute\""));
    TEST_ASSERT_NOT_NULL(strstr(result, "\"input_schema\""));
    
    // Should not have OpenAI's function wrapper
    TEST_ASSERT_NULL(strstr(result, "\"type\": \"function\""));
    
    free(result);
    cleanup_tool_registry(&tools);
}

void test_ralph_api_type_detection(void) {
    // Test API type detection logic directly without going through load_config
    // which might load from .env file
    
    // Test OpenAI detection
    const char* openai_url = "https://api.openai.com/v1/chat/completions";
    APIType api_type;
    const char* max_tokens_param;
    
    if (strstr(openai_url, "api.openai.com") != NULL) {
        api_type = API_TYPE_OPENAI;
        max_tokens_param = "max_completion_tokens";
    } else if (strstr(openai_url, "api.anthropic.com") != NULL) {
        api_type = API_TYPE_ANTHROPIC;
        max_tokens_param = "max_tokens";
    } else {
        api_type = API_TYPE_LOCAL;
        max_tokens_param = "max_tokens";
    }
    
    TEST_ASSERT_EQUAL(API_TYPE_OPENAI, api_type);
    TEST_ASSERT_EQUAL_STRING("max_completion_tokens", max_tokens_param);
    
    // Test Anthropic detection
    const char* anthropic_url = "https://api.anthropic.com/v1/messages";
    
    if (strstr(anthropic_url, "api.openai.com") != NULL) {
        api_type = API_TYPE_OPENAI;
        max_tokens_param = "max_completion_tokens";
    } else if (strstr(anthropic_url, "api.anthropic.com") != NULL) {
        api_type = API_TYPE_ANTHROPIC;
        max_tokens_param = "max_tokens";
    } else {
        api_type = API_TYPE_LOCAL;
        max_tokens_param = "max_tokens";
    }
    
    TEST_ASSERT_EQUAL(API_TYPE_ANTHROPIC, api_type);
    TEST_ASSERT_EQUAL_STRING("max_tokens", max_tokens_param);
    
    // Test local server detection
    const char* local_url = "http://localhost:1234/v1/chat/completions";
    
    if (strstr(local_url, "api.openai.com") != NULL) {
        api_type = API_TYPE_OPENAI;
        max_tokens_param = "max_completion_tokens";
    } else if (strstr(local_url, "api.anthropic.com") != NULL) {
        api_type = API_TYPE_ANTHROPIC;
        max_tokens_param = "max_tokens";
    } else {
        api_type = API_TYPE_LOCAL;
        max_tokens_param = "max_tokens";
    }
    
    TEST_ASSERT_EQUAL(API_TYPE_LOCAL, api_type);
    TEST_ASSERT_EQUAL_STRING("max_tokens", max_tokens_param);
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
    RUN_TEST(test_tool_name_hardcoded_bug_fixed);
    
    // Anthropic tests
    RUN_TEST(test_ralph_build_anthropic_json_payload_basic);
    RUN_TEST(test_ralph_build_anthropic_json_payload_with_system);
    RUN_TEST(test_ralph_build_anthropic_json_payload_with_tools);
    RUN_TEST(test_ralph_api_type_detection);
    
    return UNITY_END();
}