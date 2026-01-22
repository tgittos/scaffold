#include "unity.h"
#include "model_capabilities.h"
#include "tools_system.h"
#include "output_formatter.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test globals
static ModelRegistry* test_registry = NULL;
static ToolRegistry tool_registry;

void setUp(void) {
    // Initialize model registry
    test_registry = malloc(sizeof(ModelRegistry));
    init_model_registry(test_registry);
    
    // Register test models
    register_gpt_models(test_registry);
    register_claude_models(test_registry);
    register_qwen_models(test_registry);
    register_deepseek_models(test_registry);
    register_default_model(test_registry);
    
    // Initialize tool registry
    init_tool_registry(&tool_registry);
    register_builtin_tools(&tool_registry);
}

void tearDown(void) {
    cleanup_model_registry(test_registry);
    free(test_registry);
    test_registry = NULL;
    
    cleanup_tool_registry(&tool_registry);
}

void test_gpt_model_tool_generation(void) {
    // Test GPT model generates OpenAI-style tools JSON
    char* tools_json = generate_model_tools_json(test_registry, "gpt-4", &tool_registry);
    
    TEST_ASSERT_NOT_NULL(tools_json);
    
    // Check for OpenAI format - has "type": "function" wrapper
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"type\": \"function\""));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"function\": {"));
    
    free(tools_json);
}

void test_claude_model_tool_generation(void) {
    // Test Claude model generates Anthropic-style tools JSON
    char* tools_json = generate_model_tools_json(test_registry, "claude-3-opus", &tool_registry);
    
    TEST_ASSERT_NOT_NULL(tools_json);
    
    // Check for Anthropic format - no "type": "function" wrapper
    TEST_ASSERT_NULL(strstr(tools_json, "\"type\": \"function\""));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"name\":"));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"description\":"));
    
    free(tools_json);
}

void test_qwen_model_tool_generation(void) {
    // Test Qwen model uses OpenAI-style tool format (same as GPT)
    // First verify the model is registered
    ModelCapabilities* model = detect_model_capabilities(test_registry, "qwen2.5");
    TEST_ASSERT_NOT_NULL_MESSAGE(model, "Qwen model should be found");
    TEST_ASSERT_NOT_NULL_MESSAGE(model->generate_tools_json, "Qwen model should have generate_tools_json");

    char* tools_json = generate_model_tools_json(test_registry, "qwen2.5", &tool_registry);

    TEST_ASSERT_NOT_NULL_MESSAGE(tools_json, "generate_model_tools_json should return non-NULL for qwen");

    // Should use standard OpenAI format since qwen uses generate_tools_json
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"type\": \"function\""));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"function\""));
    // Use C-based tool that doesn't require Python
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "vector_db_search"));

    free(tools_json);
}

void test_model_tool_parsing_gpt(void) {
    const char* json_response = 
        "{\"choices\":[{\"message\":{\"tool_calls\":["
        "{\"id\":\"call_123\",\"type\":\"function\",\"function\":{\"name\":\"get_current_time\",\"arguments\":\"{}\"}}"
        "]}}]}";
    
    ToolCall* tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_model_tool_calls(test_registry, "gpt-4", json_response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_EQUAL_STRING("call_123", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("get_current_time", tool_calls[0].name);
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_model_tool_parsing_claude(void) {
    const char* json_response = 
        "{\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_123\",\"name\":\"get_current_time\",\"input\":{}}]}";
    
    ToolCall* tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_model_tool_calls(test_registry, "claude-3-opus", json_response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_EQUAL_STRING("toolu_123", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("get_current_time", tool_calls[0].name);
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_model_without_tools(void) {
    // Test that default model returns NULL for tool functions
    char* tools_json = generate_model_tools_json(test_registry, "default", &tool_registry);
    
    TEST_ASSERT_NULL(tools_json);
}

void test_unknown_model_fallback(void) {
    // Test that unknown model returns NULL (falls back to provider-specific)
    char* tools_json = generate_model_tools_json(test_registry, "unknown-model-xyz", &tool_registry);
    
    TEST_ASSERT_NULL(tools_json);
}

void test_model_tool_result_formatting(void) {
    ToolResult result = {
        .tool_call_id = "call_123",
        .result = "The current time is 2024-01-15 10:30:00 UTC",
        .success = 1
    };
    
    // Test GPT model formatting
    char* gpt_msg = format_model_tool_result_message(test_registry, "gpt-4", &result);
    TEST_ASSERT_NOT_NULL(gpt_msg);
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "call_123"));
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, result.result));
    free(gpt_msg);
    
    // Test Claude model formatting
    char* claude_msg = format_model_tool_result_message(test_registry, "claude-3-opus", &result);
    TEST_ASSERT_NOT_NULL(claude_msg);
    TEST_ASSERT_NOT_NULL(strstr(claude_msg, "call_123"));
    TEST_ASSERT_NOT_NULL(strstr(claude_msg, result.result));
    free(claude_msg);
}

void test_model_assistant_tool_message_formatting(void) {
    ToolCall tool_calls[] = {
        {
            .id = "call_123",
            .name = "get_weather",
            .arguments = "{\"location\":\"New York\"}"
        }
    };
    
    // Test GPT model formatting
    char* gpt_msg = format_model_assistant_tool_message(test_registry, "gpt-4", 
                                                       "Let me check the weather for you.",
                                                       tool_calls, 1);
    TEST_ASSERT_NOT_NULL(gpt_msg);
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "\"tool_calls\":"));
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "call_123"));
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "get_weather"));
    free(gpt_msg);
    
    // Test Claude model formatting (preserves raw JSON)
    const char* claude_raw = "{\"content\":[{\"type\":\"text\",\"text\":\"Let me check.\"},{\"type\":\"tool_use\",\"id\":\"toolu_123\",\"name\":\"get_weather\",\"input\":{\"location\":\"New York\"}}]}";
    char* claude_msg = format_model_assistant_tool_message(test_registry, "claude-3-opus",
                                                          claude_raw, NULL, 0);
    TEST_ASSERT_NOT_NULL(claude_msg);
    TEST_ASSERT_EQUAL_STRING(claude_raw, claude_msg);
    free(claude_msg);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_gpt_model_tool_generation);
    RUN_TEST(test_claude_model_tool_generation);
    RUN_TEST(test_qwen_model_tool_generation);
    RUN_TEST(test_model_tool_parsing_gpt);
    RUN_TEST(test_model_tool_parsing_claude);
    RUN_TEST(test_model_without_tools);
    RUN_TEST(test_unknown_model_fallback);
    RUN_TEST(test_model_tool_result_formatting);
    RUN_TEST(test_model_assistant_tool_message_formatting);
    
    return UNITY_END();
}