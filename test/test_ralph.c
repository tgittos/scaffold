#include "unity.h"
#include "ralph.h"
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
    
    return UNITY_END();
}