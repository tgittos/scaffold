#include "../../test/unity/unity.h"
#include "../../src/tools/memory_tool.h"
#include "../../src/tools/tools_system.h"
#include "../../src/db/vector_db.h"
#include "../../src/llm/embeddings.h"
#include "../../src/llm/embeddings_service.h"
#include "../../src/utils/ralph_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global storage for API key preservation across setUp/tearDown
static char *g_saved_api_key = NULL;

void setUp(void) {
    // Initialize ralph home directory (required for document store)
    ralph_home_init(NULL);

    // Save the API key at the start of each test
    const char *key = getenv("OPENAI_API_KEY");
    if (key) {
        g_saved_api_key = strdup(key);
    } else {
        g_saved_api_key = NULL;
    }
}

void tearDown(void) {
    // Restore the API key after each test (handles test failures that exit early)
    if (g_saved_api_key) {
        setenv("OPENAI_API_KEY", g_saved_api_key, 1);
        free(g_saved_api_key);
        g_saved_api_key = NULL;
    }

    // Cleanup ralph home
    ralph_home_cleanup();
}

void test_register_memory_tools(void) {
    ToolRegistry registry = {0};
    init_tool_registry(&registry);
    
    int initial_count = registry.function_count;
    int result = register_memory_tools(&registry);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Should have registered 3 tools
    TEST_ASSERT_EQUAL_INT(initial_count + 3, registry.function_count);
    
    // Check tool names
    int found_remember = 0;
    int found_recall = 0;
    int found_forget = 0;
    
    for (int i = 0; i < registry.function_count; i++) {
        if (strcmp(registry.functions[i].name, "remember") == 0) {
            found_remember = 1;
            TEST_ASSERT_EQUAL_STRING("Store important information in long-term memory for future reference", 
                                   registry.functions[i].description);
            TEST_ASSERT_EQUAL_INT(4, registry.functions[i].parameter_count);
        }
        if (strcmp(registry.functions[i].name, "recall_memories") == 0) {
            found_recall = 1;
            TEST_ASSERT_EQUAL_STRING("Search and retrieve relevant memories based on a query", 
                                   registry.functions[i].description);
            TEST_ASSERT_EQUAL_INT(2, registry.functions[i].parameter_count);
        }
        if (strcmp(registry.functions[i].name, "forget_memory") == 0) {
            found_forget = 1;
            TEST_ASSERT_EQUAL_STRING("Delete a specific memory from long-term storage by its ID", 
                                   registry.functions[i].description);
            TEST_ASSERT_EQUAL_INT(1, registry.functions[i].parameter_count);
        }
    }
    
    TEST_ASSERT_TRUE(found_remember);
    TEST_ASSERT_TRUE(found_recall);
    TEST_ASSERT_TRUE(found_forget);
    
    cleanup_tool_registry(&registry);
}

void test_remember_tool_missing_content(void) {
    ToolCall tool_call = {
        .id = "test_id",
        .name = "remember",
        .arguments = "{\"type\": \"fact\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_remember_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "Missing required parameter: content") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_remember_tool_no_api_key(void) {
    // Temporarily unset API key
    char *old_key = getenv("OPENAI_API_KEY");
    char *saved_key = old_key ? strdup(old_key) : NULL;
    unsetenv("OPENAI_API_KEY");

    // Reinitialize embeddings service to pick up the unset key
    embeddings_service_reinitialize();

    ToolCall tool_call = {
        .id = "test_id",
        .name = "remember",
        .arguments = "{\"content\": \"Test memory content\"}"
    };

    ToolResult result = {0};
    int exec_result = execute_remember_tool_call(&tool_call, &result);

    // Capture test values BEFORE restoring env, so assertions don't exit early
    int got_exec_result = exec_result;
    int got_result_not_null = (result.result != NULL);
    int got_success = result.success;
    int got_error_message = (result.result && strstr(result.result, "Embeddings service not configured") != NULL);

    // Free resources before restoring
    free(result.tool_call_id);
    free(result.result);

    // Restore API key and reinitialize embeddings service BEFORE assertions
    // This ensures the env is restored even if assertions fail
    if (saved_key) {
        setenv("OPENAI_API_KEY", saved_key, 1);
        free(saved_key);
        embeddings_service_reinitialize();
    }

    // Now run assertions
    TEST_ASSERT_EQUAL_INT(0, got_exec_result);
    TEST_ASSERT_TRUE(got_result_not_null);
    TEST_ASSERT_EQUAL_INT(0, got_success);
    TEST_ASSERT_TRUE(got_error_message);
}

void test_remember_tool_with_valid_content(void) {
    // This test requires a valid OPENAI_API_KEY environment variable
    if (getenv("OPENAI_API_KEY") == NULL) {
        TEST_IGNORE_MESSAGE("OPENAI_API_KEY not set, skipping integration test");
        return;
    }

    ToolCall tool_call = {
        .id = "test_memory_id",
        .name = "remember",
        .arguments = "{\"content\": \"Ralph is a C program that uses Cosmopolitan for portability\", "
                    "\"type\": \"fact\", \"source\": \"test\", \"importance\": \"high\"}"
    };

    ToolResult result = {0};
    int exec_result = execute_remember_tool_call(&tool_call, &result);

    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "\"success\": true") != NULL);
    TEST_ASSERT_TRUE(strstr(result.result, "Memory stored successfully") != NULL);

    free(result.tool_call_id);
    free(result.result);
}

void test_recall_memories_missing_query(void) {
    ToolCall tool_call = {
        .id = "test_id",
        .name = "recall_memories",
        .arguments = "{\"k\": 5}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "Missing required parameter: query") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_recall_memories_no_api_key(void) {
    // Temporarily unset API key
    char *old_key = getenv("OPENAI_API_KEY");
    char *saved_key = old_key ? strdup(old_key) : NULL;
    unsetenv("OPENAI_API_KEY");

    // Reinitialize embeddings service to pick up the unset key
    embeddings_service_reinitialize();

    ToolCall tool_call = {
        .id = "test_id",
        .name = "recall_memories",
        .arguments = "{\"query\": \"test query\"}"
    };

    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&tool_call, &result);

    // Capture test values BEFORE restoring env, so assertions don't exit early
    int got_exec_result = exec_result;
    int got_result_not_null = (result.result != NULL);
    int got_success = result.success;
    int got_error_message = (result.result && strstr(result.result, "Embeddings service not configured") != NULL);

    // Free resources before restoring
    free(result.tool_call_id);
    free(result.result);

    // Restore API key and reinitialize embeddings service BEFORE assertions
    // This ensures the env is restored even if assertions fail
    if (saved_key) {
        setenv("OPENAI_API_KEY", saved_key, 1);
        free(saved_key);
        embeddings_service_reinitialize();
    }

    // Now run assertions
    TEST_ASSERT_EQUAL_INT(0, got_exec_result);
    TEST_ASSERT_TRUE(got_result_not_null);
    TEST_ASSERT_EQUAL_INT(0, got_success);
    TEST_ASSERT_TRUE(got_error_message);
}

void test_recall_memories_with_valid_query(void) {
    // This test requires a valid OPENAI_API_KEY environment variable
    if (getenv("OPENAI_API_KEY") == NULL) {
        TEST_IGNORE_MESSAGE("OPENAI_API_KEY not set, skipping integration test");
        return;
    }
    
    // First store a memory
    ToolCall store_call = {
        .id = "store_test",
        .name = "remember",
        .arguments = "{\"content\": \"The capital of France is Paris\", "
                    "\"type\": \"fact\", \"importance\": \"high\"}"
    };
    
    ToolResult store_result = {0};
    execute_remember_tool_call(&store_call, &store_result);
    free(store_result.tool_call_id);
    free(store_result.result);
    
    // Now try to recall it
    ToolCall recall_call = {
        .id = "recall_test",
        .name = "recall_memories",
        .arguments = "{\"query\": \"capital of France\", \"k\": 3}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&recall_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "\"success\": true") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_forget_memory_tool_missing_id(void) {
    ToolCall tool_call = {
        .id = "test_forget",
        .name = "forget_memory",
        .arguments = "{}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_forget_memory_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "Missing or invalid required parameter: memory_id") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_forget_memory_tool_nonexistent_id(void) {
    if (getenv("OPENAI_API_KEY") == NULL) {
        TEST_IGNORE_MESSAGE("OPENAI_API_KEY not set, skipping integration test");
        return;
    }
    
    ToolCall tool_call = {
        .id = "test_forget",
        .name = "forget_memory",
        .arguments = "{\"memory_id\": 999999}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_forget_memory_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "Memory with ID 999999 not found") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_memory_tool_json_escaping(void) {
    if (getenv("OPENAI_API_KEY") == NULL) {
        TEST_IGNORE_MESSAGE("OPENAI_API_KEY not set, skipping integration test");
        return;
    }
    
    // Test with content that needs escaping
    ToolCall tool_call = {
        .id = "escape_test",
        .name = "remember",
        .arguments = "{\"content\": \"This has \\\"quotes\\\" and\\nnewlines\\tand tabs\", "
                    "\"type\": \"test\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_remember_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_memory_persistence_across_calls(void) {
    if (getenv("OPENAI_API_KEY") == NULL) {
        TEST_IGNORE_MESSAGE("OPENAI_API_KEY not set, skipping integration test");
        return;
    }
    
    // Store multiple memories
    const char* memories[] = {
        "Ralph uses mbedtls for TLS support",
        "Ralph can execute shell commands",
        "Ralph supports multiple LLM providers"
    };
    
    for (int i = 0; i < 3; i++) {
        char args[512];
        snprintf(args, sizeof(args), 
                "{\"content\": \"%s\", \"type\": \"fact\", \"importance\": \"normal\"}", 
                memories[i]);
        
        ToolCall call = {
            .id = "store_multi",
            .name = "remember",
            .arguments = args
        };
        
        ToolResult result = {0};
        execute_remember_tool_call(&call, &result);
        TEST_ASSERT_EQUAL_INT(1, result.success);
        
        free(result.tool_call_id);
        free(result.result);
    }
    
    // Recall memories about Ralph
    ToolCall recall_call = {
        .id = "recall_multi",
        .name = "recall_memories",
        .arguments = "{\"query\": \"Ralph features and capabilities\", \"k\": 5}"
    };
    
    ToolResult recall_result = {0};
    int exec_result = execute_recall_memories_tool_call(&recall_call, &recall_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, recall_result.success);
    TEST_ASSERT_TRUE(strstr(recall_result.result, "memories") != NULL);
    
    free(recall_result.tool_call_id);
    free(recall_result.result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_register_memory_tools);
    RUN_TEST(test_remember_tool_missing_content);
    RUN_TEST(test_remember_tool_no_api_key);
    RUN_TEST(test_remember_tool_with_valid_content);
    RUN_TEST(test_recall_memories_missing_query);
    RUN_TEST(test_recall_memories_no_api_key);
    RUN_TEST(test_recall_memories_with_valid_query);
    RUN_TEST(test_forget_memory_tool_missing_id);
    RUN_TEST(test_forget_memory_tool_nonexistent_id);
    RUN_TEST(test_memory_tool_json_escaping);
    RUN_TEST(test_memory_persistence_across_calls);
    
    return UNITY_END();
}