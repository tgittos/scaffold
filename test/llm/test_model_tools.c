#include "unity.h"
#include "lib/llm/model_capabilities.h"
#include "lib/tools/tools_system.h"
#include "lib/tools/tool_format.h"
#include "tools/builtin_tools.h"
#include "ui/output_formatter.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util/app_home.h"

// Test globals
static ModelRegistry* test_registry = NULL;
static ToolRegistry tool_registry;

void setUp(void) {
    app_home_init(NULL);
    // Initialize model registry
    test_registry = malloc(sizeof(ModelRegistry));
    init_model_registry(test_registry);
    
    register_all_models(test_registry);
    
    // Initialize tool registry
    init_tool_registry(&tool_registry);
    register_builtin_tools(&tool_registry);
}

void tearDown(void) {
    cleanup_model_registry(test_registry);
    free(test_registry);
    test_registry = NULL;
    
    cleanup_tool_registry(&tool_registry);

    app_home_cleanup();
}

/* ---- Model registry detection ---- */

void test_registry_detects_by_substring(void) {
    ModelCapabilities* gpt = detect_model_capabilities(test_registry, "gpt-4o-mini");
    TEST_ASSERT_NOT_NULL(gpt);
    TEST_ASSERT_EQUAL_STRING("gpt", gpt->model_pattern);

    ModelCapabilities* claude = detect_model_capabilities(test_registry, "anthropic/claude-opus-5.5");
    TEST_ASSERT_NOT_NULL(claude);
    TEST_ASSERT_EQUAL_STRING("claude", claude->model_pattern);
}

void test_registry_detection_is_case_insensitive(void) {
    ModelCapabilities* m = detect_model_capabilities(test_registry, "Qwen2.5-Coder");
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_STRING("qwen", m->model_pattern);

    m = detect_model_capabilities(test_registry, "CLAUDE-3-OPUS");
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_STRING("claude", m->model_pattern);
}

void test_registry_unknown_model_falls_back_to_default(void) {
    ModelCapabilities* m = detect_model_capabilities(test_registry, "unknown-model-xyz");
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_STRING("default", m->model_pattern);
    TEST_ASSERT_EQUAL(4096, m->max_context_length);
}

void test_registry_max_context_length(void) {
    TEST_ASSERT_EQUAL(128000, detect_model_capabilities(test_registry, "gpt-4")->max_context_length);
    TEST_ASSERT_EQUAL(200000, detect_model_capabilities(test_registry, "claude-3-opus")->max_context_length);
    TEST_ASSERT_EQUAL(32768, detect_model_capabilities(test_registry, "qwen2.5")->max_context_length);
    TEST_ASSERT_EQUAL(400000, detect_model_capabilities(test_registry, "gpt-5-codex")->max_context_length);
}

void test_registry_thinking_tags(void) {
    ModelCapabilities* qwen = detect_model_capabilities(test_registry, "qwen2.5");
    TEST_ASSERT_TRUE(qwen->supports_thinking_tags);
    TEST_ASSERT_EQUAL_STRING("<think>", qwen->thinking_start_tag);
    TEST_ASSERT_EQUAL_STRING("</think>", qwen->thinking_end_tag);

    ModelCapabilities* claude = detect_model_capabilities(test_registry, "claude-3-opus");
    TEST_ASSERT_FALSE(claude->supports_thinking_tags);
    TEST_ASSERT_NULL(claude->thinking_start_tag);
    TEST_ASSERT_NULL(claude->thinking_end_tag);
}

void test_registry_process_response_set_for_all(void) {
    const char* names[] = { "gpt-4", "claude-3-opus", "qwen2.5", "unknown-model-xyz" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        ModelCapabilities* m = detect_model_capabilities(test_registry, names[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(m, names[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(m->process_response, names[i]);
    }
}

/* ---- Tool format strategies ---- */

void test_openai_strategy_tool_generation(void) {
    char* tools_json = tool_format_openai.generate_tools_json(&tool_registry);
    
    TEST_ASSERT_NOT_NULL(tools_json);
    
    // Check for OpenAI format - has "type": "function" wrapper
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"type\": \"function\""));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"function\": {"));
    // Use C-based tool that doesn't require Python
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "vector_db"));
    
    free(tools_json);
}

void test_anthropic_strategy_tool_generation(void) {
    char* tools_json = tool_format_anthropic.generate_tools_json(&tool_registry);
    
    TEST_ASSERT_NOT_NULL(tools_json);
    
    // Check for Anthropic format - no "type": "function" wrapper
    TEST_ASSERT_NULL(strstr(tools_json, "\"type\": \"function\""));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"name\":"));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "\"description\":"));
    TEST_ASSERT_NOT_NULL(strstr(tools_json, "input_schema"));
    
    free(tools_json);
}

void test_openai_strategy_tool_parsing(void) {
    const char* json_response = 
        "{\"choices\":[{\"message\":{\"tool_calls\":["
        "{\"id\":\"call_123\",\"type\":\"function\",\"function\":{\"name\":\"get_current_time\",\"arguments\":\"{}\"}}"
        "]}}]}";
    
    ToolCall* tool_calls = NULL;
    int call_count = 0;
    
    int result = tool_format_openai.parse_tool_calls(json_response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_EQUAL_STRING("call_123", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("get_current_time", tool_calls[0].name);
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_anthropic_strategy_tool_parsing(void) {
    const char* json_response = 
        "{\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_123\",\"name\":\"get_current_time\",\"input\":{}}]}";
    
    ToolCall* tool_calls = NULL;
    int call_count = 0;
    
    int result = tool_format_anthropic.parse_tool_calls(json_response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_EQUAL_STRING("toolu_123", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("get_current_time", tool_calls[0].name);
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_get_tool_format_strategy(void) {
    TEST_ASSERT_EQUAL_PTR(&tool_format_anthropic, get_tool_format_strategy("anthropic"));
    TEST_ASSERT_EQUAL_PTR(&tool_format_openai, get_tool_format_strategy("openai"));
    TEST_ASSERT_EQUAL_PTR(&tool_format_openai, get_tool_format_strategy("local"));
    TEST_ASSERT_EQUAL_PTR(&tool_format_openai, get_tool_format_strategy(NULL));
}

/* ---- Assistant tool message formatting ---- */

void test_gpt_format_assistant_tool_message(void) {
    ToolCall tool_calls[] = {
        {
            .id = "call_123",
            .name = "get_weather",
            .arguments = "{\"location\":\"New York\"}"
        }
    };
    
    char* gpt_msg = gpt_format_assistant_tool_message("Let me check the weather for you.",
                                                      tool_calls, 1);
    TEST_ASSERT_NOT_NULL(gpt_msg);
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "\"tool_calls\":"));
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "call_123"));
    TEST_ASSERT_NOT_NULL(strstr(gpt_msg, "get_weather"));
    free(gpt_msg);
}

void test_claude_format_assistant_tool_message(void) {
    // Claude formatting preserves the raw JSON
    const char* claude_raw = "{\"content\":[{\"type\":\"text\",\"text\":\"Let me check.\"},{\"type\":\"tool_use\",\"id\":\"toolu_123\",\"name\":\"get_weather\",\"input\":{\"location\":\"New York\"}}]}";
    char* claude_msg = claude_format_assistant_tool_message(claude_raw, NULL, 0);
    TEST_ASSERT_NOT_NULL(claude_msg);
    TEST_ASSERT_EQUAL_STRING(claude_raw, claude_msg);
    free(claude_msg);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_registry_detects_by_substring);
    RUN_TEST(test_registry_detection_is_case_insensitive);
    RUN_TEST(test_registry_unknown_model_falls_back_to_default);
    RUN_TEST(test_registry_max_context_length);
    RUN_TEST(test_registry_thinking_tags);
    RUN_TEST(test_registry_process_response_set_for_all);
    RUN_TEST(test_openai_strategy_tool_generation);
    RUN_TEST(test_anthropic_strategy_tool_generation);
    RUN_TEST(test_openai_strategy_tool_parsing);
    RUN_TEST(test_anthropic_strategy_tool_parsing);
    RUN_TEST(test_get_tool_format_strategy);
    RUN_TEST(test_gpt_format_assistant_tool_message);
    RUN_TEST(test_claude_format_assistant_tool_message);
    
    return UNITY_END();
}
