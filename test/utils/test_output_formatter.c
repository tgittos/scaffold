#include "unity.h"
#include "output_formatter.h"
#include <string.h>
#include <stdio.h>

// Test data - typical OpenAI response
static const char *openai_response = 
    "{"
    "\"id\":\"chatcmpl-123\","
    "\"object\":\"chat.completion\","
    "\"created\":1677652288,"
    "\"model\":\"gpt-3.5-turbo\","
    "\"choices\":[{"
        "\"index\":0,"
        "\"message\":{"
            "\"role\":\"assistant\","
            "\"content\":\"Hello! How can I help you today?\""
        "},"
        "\"finish_reason\":\"stop\""
    "}],"
    "\"usage\":{"
        "\"prompt_tokens\":9,"
        "\"completion_tokens\":12,"
        "\"total_tokens\":21"
    "}"
    "}";

// Test data - DeepSeek response with thinking
static const char *deepseek_response = 
    "{"
    "\"id\":\"chatcmpl-qkg9p3de9npcjqwrkingtq\","
    "\"object\":\"chat.completion\","
    "\"created\":1753895581,"
    "\"model\":\"deepseek/deepseek-r1-0528-qwen3-8b\","
    "\"choices\":[{"
        "\"index\":0,"
        "\"logprobs\":null,"
        "\"finish_reason\":\"length\","
        "\"message\":{"
            "\"role\":\"assistant\","
            "\"content\":\"<think>\\nUser is asking about my identity.\\n</think>\\n\\nI am DeepSeek R1, an AI assistant.\""
        "}"
    "}],"
    "\"usage\":{"
        "\"prompt_tokens\":13,"
        "\"completion_tokens\":99,"
        "\"total_tokens\":112"
    "},"
    "\"stats\":{},"
    "\"system_fingerprint\":\"deepseek/deepseek-r1-0528-qwen3-8b\""
    "}";

// Test data - minimal response without usage
static const char *minimal_response = 
    "{"
    "\"choices\":[{"
        "\"message\":{"
            "\"content\":\"Simple response\""
        "}"
    "}]"
    "}";

// Test data - tool calls response (no content field)
static const char *tool_calls_response = 
    "{"
    "\"id\":\"chatcmpl-test123\","
    "\"object\":\"chat.completion\","
    "\"created\":1753923401,"
    "\"model\":\"test/model\","
    "\"choices\":[{"
        "\"index\":0,"
        "\"message\":{"
            "\"role\":\"assistant\","
            "\"tool_calls\":[{"
                "\"id\":\"call_123\","
                "\"type\":\"function\","
                "\"function\":{"
                    "\"name\":\"shell_execute\","
                    "\"arguments\":\"{\\\"command\\\":\\\"echo test\\\"}\""
                "}"
            "}]"
        "},"
        "\"finish_reason\":\"tool_calls\""
    "}],"
    "\"usage\":{"
        "\"prompt_tokens\":100,"
        "\"completion_tokens\":25,"
        "\"total_tokens\":125"
    "}"
    "}";

// Test data - malformed JSON
static const char *malformed_response = 
    "{"
    "\"choices\":[{"
        "\"message\":{"
            "\"content\":\"Incomplete";

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

void test_parse_api_response_with_null_parameters(void) {
    ParsedResponse result;
    
    // Test null JSON response
    TEST_ASSERT_EQUAL(-1, parse_api_response(NULL, &result));
    
    // Test null result pointer
    TEST_ASSERT_EQUAL(-1, parse_api_response(openai_response, NULL));
}

void test_parse_api_response_openai_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(openai_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // OpenAI doesn't have thinking
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Hello! How can I help you today?", result.response_content);
    TEST_ASSERT_EQUAL(9, result.prompt_tokens);
    TEST_ASSERT_EQUAL(12, result.completion_tokens);
    TEST_ASSERT_EQUAL(21, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_deepseek_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(deepseek_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("\nUser is asking about my identity.\n", result.thinking_content);
    TEST_ASSERT_EQUAL_STRING("I am DeepSeek R1, an AI assistant.", result.response_content);
    TEST_ASSERT_EQUAL(13, result.prompt_tokens);
    TEST_ASSERT_EQUAL(99, result.completion_tokens);
    TEST_ASSERT_EQUAL(112, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_minimal_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(minimal_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // No thinking in minimal response
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Simple response", result.response_content);
    TEST_ASSERT_EQUAL(-1, result.prompt_tokens);
    TEST_ASSERT_EQUAL(-1, result.completion_tokens);
    TEST_ASSERT_EQUAL(-1, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_malformed_json(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(malformed_response, &result);
    
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_parse_api_response_no_content(void) {
    const char *no_content_response = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"role\":\"assistant\""
            "}"
        "}]"
        "}";
    
    ParsedResponse result;
    
    int ret = parse_api_response(no_content_response, &result);
    
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_cleanup_parsed_response_with_null_pointer(void) {
    // Should not crash
    cleanup_parsed_response(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_cleanup_parsed_response_with_null_content(void) {
    ParsedResponse result = {0};
    result.thinking_content = NULL;
    result.response_content = NULL;
    
    // Should not crash
    cleanup_parsed_response(&result);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_cleanup_parsed_response_with_allocated_content(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(openai_response, &result);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);
    
    cleanup_parsed_response(&result);
    TEST_ASSERT_NULL(result.thinking_content);
    TEST_ASSERT_NULL(result.response_content);
}

void test_print_formatted_response_with_null_parameters(void) {
    // Should not crash
    print_formatted_response(NULL);
    
    ParsedResponse result = {0};
    result.thinking_content = NULL;
    result.response_content = NULL;
    print_formatted_response(&result);
    
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_content_with_escaped_quotes(void) {
    const char *escaped_response = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"He said \\\"Hello world!\\\" to me.\""
            "}"
        "}],"
        "\"usage\":{"
            "\"total_tokens\":15"
        "}"
        "}";
    
    ParsedResponse result;
    
    int ret = parse_api_response(escaped_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // No thinking in this test
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("He said \"Hello world!\" to me.", result.response_content);
    TEST_ASSERT_EQUAL(15, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_tool_calls_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(tool_calls_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // No content in tool calls
    TEST_ASSERT_NULL(result.response_content);  // No content in tool calls
    TEST_ASSERT_EQUAL(100, result.prompt_tokens);
    TEST_ASSERT_EQUAL(25, result.completion_tokens);
    TEST_ASSERT_EQUAL(125, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

// Anthropic response tests
void test_parse_anthropic_response_basic(void) {
    const char *anthropic_response = 
        "{"
        "\"content\": [{\"type\": \"text\", \"text\": \"Hello from Anthropic!\"}],"
        "\"usage\": {\"input_tokens\": 10, \"output_tokens\": 5}"
        "}";
    
    ParsedResponse result;
    int ret = parse_anthropic_response(anthropic_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Hello from Anthropic!", result.response_content);
    TEST_ASSERT_EQUAL(10, result.prompt_tokens);
    TEST_ASSERT_EQUAL(5, result.completion_tokens);
    TEST_ASSERT_EQUAL(15, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_anthropic_response_with_thinking(void) {
    const char *anthropic_response = 
        "{"
        "\"content\": [{\"type\": \"text\", \"text\": \"<think>I need to think about this.</think>\\n\\nThe answer is 42.\"}],"
        "\"usage\": {\"input_tokens\": 20, \"output_tokens\": 15}"
        "}";
    
    ParsedResponse result;
    int ret = parse_anthropic_response(anthropic_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("I need to think about this.", result.thinking_content);
    TEST_ASSERT_EQUAL_STRING("The answer is 42.", result.response_content);
    TEST_ASSERT_EQUAL(20, result.prompt_tokens);
    TEST_ASSERT_EQUAL(15, result.completion_tokens);
    TEST_ASSERT_EQUAL(35, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_anthropic_response_null_parameters(void) {
    ParsedResponse result;
    
    // Test null JSON response
    TEST_ASSERT_EQUAL(-1, parse_anthropic_response(NULL, &result));
    
    // Test null result pointer
    const char *valid_response = "{\"content\": [{\"type\": \"text\", \"text\": \"test\"}]}";
    TEST_ASSERT_EQUAL(-1, parse_anthropic_response(valid_response, NULL));
}

void test_parse_anthropic_response_malformed(void) {
    const char *malformed_response = "{\"invalid\": \"json structure\"}";

    ParsedResponse result;
    int ret = parse_anthropic_response(malformed_response, &result);

    TEST_ASSERT_EQUAL(-1, ret);
}

// Test Anthropic extended thinking format with separate thinking content blocks
void test_parse_anthropic_response_extended_thinking(void) {
    // This is the real format Anthropic returns for extended thinking models
    const char *extended_thinking_response =
        "{"
        "\"content\": ["
            "{\"type\": \"thinking\", \"thinking\": \"Let me analyze this request carefully.\"},"
            "{\"type\": \"text\", \"text\": \"Here is my response to your question.\"}"
        "],"
        "\"usage\": {\"input_tokens\": 100, \"output_tokens\": 50}"
        "}";

    ParsedResponse result;
    int ret = parse_anthropic_response(extended_thinking_response, &result);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Let me analyze this request carefully.", result.thinking_content);
    TEST_ASSERT_EQUAL_STRING("Here is my response to your question.", result.response_content);
    TEST_ASSERT_EQUAL(100, result.prompt_tokens);
    TEST_ASSERT_EQUAL(50, result.completion_tokens);
    TEST_ASSERT_EQUAL(150, result.total_tokens);

    cleanup_parsed_response(&result);
}

// Test that thinking content containing "text": doesn't break parsing
void test_parse_anthropic_response_thinking_contains_text_field(void) {
    // Thinking content that mentions "text" field could confuse naive strstr parsing
    const char *tricky_response =
        "{"
        "\"content\": ["
            "{\"type\": \"thinking\", \"thinking\": \"The \\\"text\\\": field in JSON is important.\"},"
            "{\"type\": \"text\", \"text\": \"The answer is 42.\"}"
        "],"
        "\"usage\": {\"input_tokens\": 30, \"output_tokens\": 20}"
        "}";

    ParsedResponse result;
    int ret = parse_anthropic_response(tricky_response, &result);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("The answer is 42.", result.response_content);

    cleanup_parsed_response(&result);
}

// Test Anthropic response with multiple thinking and text blocks
void test_parse_anthropic_response_multiple_blocks(void) {
    // Rare but possible: multiple thinking and text blocks in sequence
    const char *multi_block_response =
        "{"
        "\"content\": ["
            "{\"type\": \"thinking\", \"thinking\": \"First thought.\"},"
            "{\"type\": \"thinking\", \"thinking\": \"Second thought.\"},"
            "{\"type\": \"text\", \"text\": \"First part of response.\"},"
            "{\"type\": \"text\", \"text\": \"Second part of response.\"}"
        "],"
        "\"usage\": {\"input_tokens\": 50, \"output_tokens\": 40}"
        "}";

    ParsedResponse result;
    int ret = parse_anthropic_response(multi_block_response, &result);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    // Blocks should be concatenated with newlines
    TEST_ASSERT_EQUAL_STRING("First thought.\nSecond thought.", result.thinking_content);
    TEST_ASSERT_EQUAL_STRING("First part of response.\nSecond part of response.", result.response_content);
    TEST_ASSERT_EQUAL(50, result.prompt_tokens);
    TEST_ASSERT_EQUAL(40, result.completion_tokens);
    TEST_ASSERT_EQUAL(90, result.total_tokens);

    cleanup_parsed_response(&result);
}

void test_filter_tool_call_markup_from_response(void) {
    // Test response from local model with tool call markup
    const char *local_model_response =
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"I'll help you with that task. <tool_call>{\\\"name\\\": \\\"file_read\\\", \\\"arguments\\\": {\\\"file_path\\\": \\\"/test/file.txt\\\"}}</tool_call> Let me read the file for you.\""
            "}"
        "}]"
        "}";

    ParsedResponse result;
    int ret = parse_api_response(local_model_response, &result);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);

    // The response content should NOT contain the raw <tool_call> markup
    TEST_ASSERT_NULL(strstr(result.response_content, "<tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "</tool_call>"));

    // But should contain the descriptive text
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "I'll help you"));
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "Let me read the file"));

    cleanup_parsed_response(&result);
}

// Test tool argument display in log_tool_execution_improved
void test_tool_argument_display_basic(void) {
    printf("\n--- Testing tool argument display ---\n");

    // Test read_file with path argument
    log_tool_execution_improved("read_file", "{\"path\": \"/home/user/test.txt\"}", true, "File contents");

    // Test shell with command argument
    log_tool_execution_improved("shell", "{\"command\": \"git status\"}", true, "On branch main");

    // Test write_file with path
    log_tool_execution_improved("write_file", "{\"path\": \"/tmp/output.txt\", \"content\": \"hello world\"}", true, "Written");

    // Test web_fetch with url
    log_tool_execution_improved("web_fetch", "{\"url\": \"https://example.com/api\"}", true, "Response data");

    // Test search with pattern and path - should show "path → pattern"
    log_tool_execution_improved("search_files", "{\"path\": \".\", \"pattern\": \"TODO\"}", true, "Found files");

    // Test search with pattern only - should show ". → pattern" (default path)
    log_tool_execution_improved("search_files", "{\"pattern\": \"*.py\"}", true, "Found files");

    // Test memory with key
    log_tool_execution_improved("memory_read", "{\"key\": \"user_preferences\"}", true, "Memory value");

    TEST_ASSERT_TRUE(1);
}

void test_tool_argument_truncation(void) {
    printf("\n--- Testing argument truncation ---\n");

    // Long path should be truncated
    log_tool_execution_improved("read_file",
        "{\"path\": \"/very/long/path/that/should/be/truncated/because/it/exceeds/max/display/length/file.txt\"}",
        true, "Contents");

    // Long command should be truncated
    log_tool_execution_improved("shell",
        "{\"command\": \"find /usr -name '*.so' -exec ls -la {} \\\\; | grep lib | head -20 | sort | uniq\"}",
        true, "Output");

    TEST_ASSERT_TRUE(1);
}

void test_tool_argument_edge_cases(void) {
    printf("\n--- Testing edge cases ---\n");

    // Empty arguments should not crash
    log_tool_execution_improved("some_tool", "{}", true, "Result");

    // NULL arguments should not crash
    log_tool_execution_improved("another_tool", NULL, true, "Result");

    // Invalid JSON should not crash (graceful fallback)
    log_tool_execution_improved("broken_tool", "not valid json {", true, "Result");

    // Empty string arguments should not crash
    log_tool_execution_improved("empty_arg_tool", "", true, "Result");

    TEST_ASSERT_TRUE(1);
}

void test_tool_argument_failure_display(void) {
    printf("\n--- Testing failure display ---\n");

    // Failure with argument should show both path and error
    log_tool_execution_improved("read_file", "{\"path\": \"/nonexistent/file.txt\"}", false, "File not found");

    TEST_ASSERT_TRUE(1);
}

void test_todowrite_display(void) {
    printf("\n--- Testing TodoWrite display ---\n");

    // Single task should show "1 task: <content>"
    log_tool_execution_improved("TodoWrite",
        "{\"todos\": [{\"content\": \"Implement feature X\", \"status\": \"pending\"}]}",
        true, "Todos updated");

    // Multiple tasks should show count and first task
    log_tool_execution_improved("TodoWrite",
        "{\"todos\": [{\"content\": \"First task\", \"status\": \"pending\"}, "
        "{\"content\": \"Second task\", \"status\": \"in_progress\"}]}",
        true, "Todos updated");

    // Long content should be truncated
    log_tool_execution_improved("TodoWrite",
        "{\"todos\": [{\"content\": \"This is a very long task description that should be truncated for display purposes\", \"status\": \"pending\"}]}",
        true, "Todos updated");

    // Empty todos array should show "updated"
    log_tool_execution_improved("TodoWrite", "{\"todos\": []}", true, "Todos updated");

    // Malformed JSON should not crash
    log_tool_execution_improved("TodoWrite", "invalid json", true, "Todos updated");

    TEST_ASSERT_TRUE(1);
}

void test_search_files_display(void) {
    printf("\n--- Testing search_files display ---\n");

    // search_files with path and pattern should show "path → pattern"
    log_tool_execution_improved("search_files",
        "{\"path\": \"src/\", \"pattern\": \"function_name\"}",
        true, "Found matches");

    // search_files with current dir path should show ". → pattern"
    log_tool_execution_improved("search_files",
        "{\"path\": \".\", \"pattern\": \"TODO\"}",
        true, "Found matches");

    // search_files with pattern only (no path) should default to ". → pattern"
    log_tool_execution_improved("search_files",
        "{\"pattern\": \"import.*os\"}",
        true, "Found matches");

    // search_files with long pattern should truncate appropriately
    log_tool_execution_improved("search_files",
        "{\"path\": \"/some/path\", \"pattern\": \"this is a very long pattern that might need truncation for display\"}",
        true, "Found matches");

    TEST_ASSERT_TRUE(1);
}

void test_task_tool_display(void) {
    printf("\n--- Testing task tool display ---\n");

    // TaskCreate should show subject
    log_tool_execution_improved("TaskCreate",
        "{\"subject\": \"Implement authentication\", \"description\": \"Add user login flow\"}",
        true, "Task created");

    // TaskUpdate should show taskId and status
    log_tool_execution_improved("TaskUpdate",
        "{\"taskId\": \"123\", \"status\": \"completed\"}",
        true, "Task updated");

    // TaskUpdate with just taskId
    log_tool_execution_improved("TaskUpdate",
        "{\"taskId\": \"456\", \"description\": \"Updated description\"}",
        true, "Task updated");

    TEST_ASSERT_TRUE(1);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_parse_api_response_with_null_parameters);
    RUN_TEST(test_parse_api_response_openai_format);
    RUN_TEST(test_parse_api_response_deepseek_format);
    RUN_TEST(test_parse_api_response_minimal_format);
    RUN_TEST(test_parse_api_response_malformed_json);
    RUN_TEST(test_parse_api_response_no_content);
    RUN_TEST(test_cleanup_parsed_response_with_null_pointer);
    RUN_TEST(test_cleanup_parsed_response_with_null_content);
    RUN_TEST(test_cleanup_parsed_response_with_allocated_content);
    RUN_TEST(test_print_formatted_response_with_null_parameters);
    RUN_TEST(test_content_with_escaped_quotes);
    RUN_TEST(test_parse_api_response_tool_calls_format);
    
    // Anthropic tests
    RUN_TEST(test_parse_anthropic_response_basic);
    RUN_TEST(test_parse_anthropic_response_with_thinking);
    RUN_TEST(test_parse_anthropic_response_null_parameters);
    RUN_TEST(test_parse_anthropic_response_malformed);
    RUN_TEST(test_parse_anthropic_response_extended_thinking);
    RUN_TEST(test_parse_anthropic_response_thinking_contains_text_field);
    RUN_TEST(test_parse_anthropic_response_multiple_blocks);

    // Tool call markup filtering test
    RUN_TEST(test_filter_tool_call_markup_from_response);

    // Tool argument display tests
    RUN_TEST(test_tool_argument_display_basic);
    RUN_TEST(test_tool_argument_truncation);
    RUN_TEST(test_tool_argument_edge_cases);
    RUN_TEST(test_tool_argument_failure_display);

    // Todo/Task tool display tests
    RUN_TEST(test_todowrite_display);
    RUN_TEST(test_search_files_display);
    RUN_TEST(test_task_tool_display);

    return UNITY_END();
}