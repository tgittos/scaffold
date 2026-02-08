#include "../../test/unity/unity.h"
#include "tools/memory_tool.h"
#include "tools/tools_system.h"
#include "db/vector_db.h"
#include "llm/embeddings.h"
#include "llm/embeddings_service.h"
#include "services/services.h"
#include "util/config.h"
#include "util/ralph_home.h"
#include "../mock_api_server.h"
#include "../mock_embeddings.h"
#include "../mock_embeddings_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../test_fs_utils.h"

extern void hnswlib_clear_all(void);

#define MOCK_GROUP_RALPH      6
#define MOCK_GROUP_GEOGRAPHY  7

static char g_test_home[256];
static char *saved_ralph_config_backup = NULL;
static char *saved_openai_api_key = NULL;
static char *saved_openai_api_url = NULL;
static MockAPIServer mock_server;
static MockAPIResponse mock_responses[1];
static Services* test_services = NULL;

void setUp(void) {
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/test_memory_tool_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_test_home));
    ralph_home_init(g_test_home);

    // Back up existing ralph.config.json file if it exists
    FILE *ralph_config_file = fopen("ralph.config.json", "r");
    if (ralph_config_file) {
        fseek(ralph_config_file, 0, SEEK_END);
        long file_size = ftell(ralph_config_file);
        fseek(ralph_config_file, 0, SEEK_SET);

        saved_ralph_config_backup = malloc(file_size + 1);
        if (saved_ralph_config_backup) {
            fread(saved_ralph_config_backup, 1, file_size, ralph_config_file);
            saved_ralph_config_backup[file_size] = '\0';
        }
        fclose(ralph_config_file);
        remove("ralph.config.json");
    }

    // Initialize mock embeddings with semantic groups
    mock_embeddings_init_test_groups();

    // Ralph-related content group
    mock_embeddings_assign_to_group("Ralph", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("Cosmopolitan", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("portability", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("mbedtls", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("TLS", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("shell command", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("LLM provider", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("features", MOCK_GROUP_RALPH);
    mock_embeddings_assign_to_group("capabilities", MOCK_GROUP_RALPH);

    // Geography content group
    mock_embeddings_assign_to_group("capital", MOCK_GROUP_GEOGRAPHY);
    mock_embeddings_assign_to_group("France", MOCK_GROUP_GEOGRAPHY);
    mock_embeddings_assign_to_group("Paris", MOCK_GROUP_GEOGRAPHY);

    // Set up mock embeddings server
    memset(&mock_server, 0, sizeof(mock_server));
    mock_server.port = 18892;
    mock_responses[0] = mock_embeddings_server_response();
    mock_server.responses = mock_responses;
    mock_server.response_count = 1;

    mock_api_server_start(&mock_server);
    mock_api_server_wait_ready(&mock_server, 2000);

    // Save original env vars
    const char *orig_key = getenv("OPENAI_API_KEY");
    const char *orig_url = getenv("OPENAI_API_URL");
    saved_openai_api_key = orig_key ? strdup(orig_key) : NULL;
    saved_openai_api_url = orig_url ? strdup(orig_url) : NULL;

    // Set env vars to point to mock server
    setenv("OPENAI_API_URL", "http://127.0.0.1:18892/api.openai.com/v1/embeddings", 1);
    setenv("OPENAI_API_KEY", "mock-test-key", 1);

    // Initialize config system
    config_init();

    // Create services container
    test_services = services_create_default();

    // Force embeddings service to pick up mock config
    embeddings_service_t* embeddings = services_get_embeddings(test_services);
    if (embeddings) {
        embeddings_service_reinitialize(embeddings);
    }

    // Register memory tools so g_services is set for execute_* functions
    ToolRegistry registry = {0};
    init_tool_registry(&registry);
    registry.services = test_services;
    register_memory_tools(&registry);
    cleanup_tool_registry(&registry);
}

void tearDown(void) {
    config_cleanup();
    remove("ralph.config.json");

    // Restore backed up ralph.config.json file if it existed
    if (saved_ralph_config_backup) {
        FILE *ralph_config_file = fopen("ralph.config.json", "w");
        if (ralph_config_file) {
            fwrite(saved_ralph_config_backup, 1, strlen(saved_ralph_config_backup), ralph_config_file);
            fclose(ralph_config_file);
        }
        free(saved_ralph_config_backup);
        saved_ralph_config_backup = NULL;
    }

    // Restore original env vars
    if (saved_openai_api_key) {
        setenv("OPENAI_API_KEY", saved_openai_api_key, 1);
        free(saved_openai_api_key);
        saved_openai_api_key = NULL;
    } else {
        unsetenv("OPENAI_API_KEY");
    }
    if (saved_openai_api_url) {
        setenv("OPENAI_API_URL", saved_openai_api_url, 1);
        free(saved_openai_api_url);
        saved_openai_api_url = NULL;
    } else {
        unsetenv("OPENAI_API_URL");
    }

    mock_api_server_stop(&mock_server);
    mock_embeddings_cleanup();

    if (test_services) {
        services_destroy(test_services);
        test_services = NULL;
    }

    hnswlib_clear_all();
    rmdir_recursive(g_test_home);
    ralph_home_cleanup();
}

void test_register_memory_tools(void) {
    ToolRegistry registry = {0};
    init_tool_registry(&registry);
    registry.services = test_services;

    int initial_count = registry.functions.count;
    int result = register_memory_tools(&registry);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should have registered 3 tools
    TEST_ASSERT_EQUAL_INT(initial_count + 3, registry.functions.count);

    // Check tool names
    int found_remember = 0;
    int found_recall = 0;
    int found_forget = 0;

    for (size_t i = 0; i < registry.functions.count; i++) {
        if (strcmp(registry.functions.data[i].name, "remember") == 0) {
            found_remember = 1;
            TEST_ASSERT_EQUAL_STRING("Store important information in long-term memory for future reference",
                                   registry.functions.data[i].description);
            TEST_ASSERT_EQUAL_INT(4, registry.functions.data[i].parameter_count);
        }
        if (strcmp(registry.functions.data[i].name, "recall_memories") == 0) {
            found_recall = 1;
            TEST_ASSERT_EQUAL_STRING("Search and retrieve relevant memories based on a query",
                                   registry.functions.data[i].description);
            TEST_ASSERT_EQUAL_INT(2, registry.functions.data[i].parameter_count);
        }
        if (strcmp(registry.functions.data[i].name, "forget_memory") == 0) {
            found_forget = 1;
            TEST_ASSERT_EQUAL_STRING("Delete a specific memory from long-term storage by its ID",
                                   registry.functions.data[i].description);
            TEST_ASSERT_EQUAL_INT(1, registry.functions.data[i].parameter_count);
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
    // Clear the API key env var to simulate unconfigured state
    unsetenv("OPENAI_API_KEY");
    embeddings_service_t* embeddings = services_get_embeddings(test_services);
    if (embeddings) embeddings_service_reinitialize(embeddings);

    ToolCall tool_call = {
        .id = "test_id",
        .name = "remember",
        .arguments = "{\"content\": \"Test memory content\"}"
    };

    ToolResult result = {0};
    int exec_result = execute_remember_tool_call(&tool_call, &result);

    int got_exec_result = exec_result;
    int got_result_not_null = (result.result != NULL);
    int got_success = result.success;
    int got_error_message = (result.result && strstr(result.result, "Embeddings service not configured") != NULL);

    free(result.tool_call_id);
    free(result.result);

    // Restore API key for subsequent tests
    setenv("OPENAI_API_KEY", "mock-test-key", 1);
    if (embeddings) embeddings_service_reinitialize(embeddings);

    TEST_ASSERT_EQUAL_INT(0, got_exec_result);
    TEST_ASSERT_TRUE(got_result_not_null);
    TEST_ASSERT_EQUAL_INT(0, got_success);
    TEST_ASSERT_TRUE(got_error_message);
}

void test_remember_tool_with_valid_content(void) {
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
    // Clear the API key env var to simulate unconfigured state
    unsetenv("OPENAI_API_KEY");
    embeddings_service_t* embeddings = services_get_embeddings(test_services);
    if (embeddings) embeddings_service_reinitialize(embeddings);

    ToolCall tool_call = {
        .id = "test_id",
        .name = "recall_memories",
        .arguments = "{\"query\": \"test query\"}"
    };

    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&tool_call, &result);

    int got_exec_result = exec_result;
    int got_result_not_null = (result.result != NULL);
    int got_success = result.success;
    int got_error_message = (result.result && strstr(result.result, "Embeddings service not configured") != NULL);

    free(result.tool_call_id);
    free(result.result);

    // Restore API key for subsequent tests
    setenv("OPENAI_API_KEY", "mock-test-key", 1);
    if (embeddings) embeddings_service_reinitialize(embeddings);

    TEST_ASSERT_EQUAL_INT(0, got_exec_result);
    TEST_ASSERT_TRUE(got_result_not_null);
    TEST_ASSERT_EQUAL_INT(0, got_success);
    TEST_ASSERT_TRUE(got_error_message);
}

void test_recall_memories_with_valid_query(void) {
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
