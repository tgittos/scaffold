#include "../../test/unity/unity.h"
#include "../../src/tools/vector_db_tool.h"
#include "../../src/tools/tools_system.h"
#include "../../src/db/vector_db.h"
#include "../../src/utils/config.h"
#include "../../src/utils/ralph_home.h"
#include "../mock_api_server.h"
#include "../mock_embeddings.h"
#include "../mock_embeddings_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

static char *saved_ralph_config_backup = NULL;
static MockAPIServer mock_server;
static MockAPIResponse mock_responses[1];

void setUp(void) {
    // Initialize ralph home directory (required for document store)
    ralph_home_init(NULL);

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
        remove("ralph.config.json");  // Remove temporarily
    }

    // Initialize mock embeddings with semantic groups
    mock_embeddings_init_test_groups();

    // Set up mock embeddings server
    memset(&mock_server, 0, sizeof(mock_server));
    mock_server.port = 18890;
    mock_responses[0] = mock_embeddings_server_response();
    mock_server.responses = mock_responses;
    mock_server.response_count = 1;

    mock_api_server_start(&mock_server);
    mock_api_server_wait_ready(&mock_server, 2000);

    // Initialize config system first
    config_init();

    // Set mock embedding API URL via config system
    config_set("embedding_api_url", "http://127.0.0.1:18890/v1/embeddings");
}

void tearDown(void) {
    // Clean up after each test
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

    // Stop mock server
    mock_api_server_stop(&mock_server);

    // Cleanup mock embeddings
    mock_embeddings_cleanup();

    // Cleanup ralph home
    ralph_home_cleanup();
}

void test_register_vector_db_tool(void) {
    ToolRegistry registry = {0};
    init_tool_registry(&registry);
    
    int result = register_vector_db_tool(&registry);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Should have registered 13 tools (added search_text and search_by_time)
    TEST_ASSERT_EQUAL_INT(13, registry.function_count);
    
    // Check tool names
    const char *expected_tools[] = {
        "vector_db_create_index",
        "vector_db_delete_index",
        "vector_db_list_indices",
        "vector_db_add_vector",
        "vector_db_update_vector",
        "vector_db_delete_vector",
        "vector_db_get_vector",
        "vector_db_search",
        "vector_db_add_text",
        "vector_db_add_chunked_text",
        "vector_db_add_pdf_document"
    };
    
    for (int i = 0; i < 11; i++) {
        TEST_ASSERT_EQUAL_STRING(expected_tools[i], registry.functions[i].name);
    }
    
    cleanup_tool_registry(&registry);
}

void test_vector_db_create_index(void) {
    ToolCall tool_call = {
        .id = "test_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"test_index\", \"dimension\": 128}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_vector_db_create_index_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "\"success\": true") != NULL);
    TEST_ASSERT_TRUE(strstr(result.result, "test_index") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_vector_db_create_index_with_all_params(void) {
    ToolCall tool_call = {
        .id = "test_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"advanced_index\", \"dimension\": 256, "
                    "\"max_elements\": 50000, \"M\": 32, \"ef_construction\": 400, "
                    "\"metric\": \"cosine\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_vector_db_create_index_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_TRUE(strstr(result.result, "\"success\": true") != NULL);
    
    free(result.tool_call_id);
    free(result.result);
}

void test_vector_db_list_indices(void) {
    // First create an index
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"list_test_index\", \"dimension\": 64}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    // Now list indices
    ToolCall list_call = {
        .id = "list_id",
        .name = "vector_db_list_indices",
        .arguments = "{}"
    };
    
    ToolResult list_result = {0};
    int exec_result = execute_vector_db_list_indices_tool_call(&list_call, &list_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(list_result.result);
    TEST_ASSERT_EQUAL_INT(1, list_result.success);
    TEST_ASSERT_TRUE(strstr(list_result.result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(list_result.result, "list_test_index") != NULL);
    
    free(list_result.tool_call_id);
    free(list_result.result);
}

void test_vector_db_add_vector(void) {
    // First create an index
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"add_test_index\", \"dimension\": 3}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    // Add a vector
    ToolCall add_call = {
        .id = "add_id",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"add_test_index\", \"vector\": [1.0, 2.0, 3.0]}"
    };
    
    ToolResult add_result = {0};
    int exec_result = execute_vector_db_add_vector_tool_call(&add_call, &add_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(add_result.result);
    TEST_ASSERT_EQUAL_INT(1, add_result.success);
    TEST_ASSERT_TRUE(strstr(add_result.result, "\"success\": true") != NULL);
    TEST_ASSERT_TRUE(strstr(add_result.result, "\"label\": 0") != NULL);
    
    free(add_result.tool_call_id);
    free(add_result.result);
}

void test_vector_db_get_vector(void) {
    // First create an index and add a vector
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"get_test_index\", \"dimension\": 3}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    ToolCall add_call = {
        .id = "add_id",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"get_test_index\", \"vector\": [4.0, 5.0, 6.0]}"
    };
    
    ToolResult add_result = {0};
    execute_vector_db_add_vector_tool_call(&add_call, &add_result);
    free(add_result.tool_call_id);
    free(add_result.result);
    
    // Get the vector
    ToolCall get_call = {
        .id = "get_id",
        .name = "vector_db_get_vector",
        .arguments = "{\"index_name\": \"get_test_index\", \"label\": 0}"
    };
    
    ToolResult get_result = {0};
    int exec_result = execute_vector_db_get_vector_tool_call(&get_call, &get_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(get_result.result);
    TEST_ASSERT_EQUAL_INT(1, get_result.success);
    TEST_ASSERT_TRUE(strstr(get_result.result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(get_result.result, "4") != NULL);
    TEST_ASSERT_TRUE(strstr(get_result.result, "5") != NULL);
    TEST_ASSERT_TRUE(strstr(get_result.result, "6") != NULL);
    
    free(get_result.tool_call_id);
    free(get_result.result);
}

void test_vector_db_search(void) {
    // Create index and add multiple vectors
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"search_test_index\", \"dimension\": 3}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    // Add first vector
    ToolCall add_call1 = {
        .id = "add_id1",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"search_test_index\", \"vector\": [1.0, 0.0, 0.0]}"
    };
    
    ToolResult add_result1 = {0};
    execute_vector_db_add_vector_tool_call(&add_call1, &add_result1);
    free(add_result1.tool_call_id);
    free(add_result1.result);
    
    // Add second vector
    ToolCall add_call2 = {
        .id = "add_id2",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"search_test_index\", \"vector\": [0.0, 1.0, 0.0]}"
    };
    
    ToolResult add_result2 = {0};
    execute_vector_db_add_vector_tool_call(&add_call2, &add_result2);
    free(add_result2.tool_call_id);
    free(add_result2.result);
    
    // Add third vector
    ToolCall add_call3 = {
        .id = "add_id3",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"search_test_index\", \"vector\": [0.0, 0.0, 1.0]}"
    };
    
    ToolResult add_result3 = {0};
    execute_vector_db_add_vector_tool_call(&add_call3, &add_result3);
    free(add_result3.tool_call_id);
    free(add_result3.result);
    
    // Search for nearest neighbors
    ToolCall search_call = {
        .id = "search_id",
        .name = "vector_db_search",
        .arguments = "{\"index_name\": \"search_test_index\", \"query_vector\": [1.0, 0.1, 0.0], \"k\": 2}"
    };
    
    ToolResult search_result = {0};
    int exec_result = execute_vector_db_search_tool_call(&search_call, &search_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(search_result.result);
    TEST_ASSERT_EQUAL_INT(1, search_result.success);
    TEST_ASSERT_TRUE(strstr(search_result.result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(search_result.result, "\"results\"") != NULL);
    
    free(search_result.tool_call_id);
    free(search_result.result);
}

void test_vector_db_update_vector(void) {
    // Create index and add a vector
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"update_test_index\", \"dimension\": 3}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    ToolCall add_call = {
        .id = "add_id",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"update_test_index\", \"vector\": [1.0, 1.0, 1.0]}"
    };
    
    ToolResult add_result = {0};
    execute_vector_db_add_vector_tool_call(&add_call, &add_result);
    free(add_result.tool_call_id);
    free(add_result.result);
    
    // Update the vector
    ToolCall update_call = {
        .id = "update_id",
        .name = "vector_db_update_vector",
        .arguments = "{\"index_name\": \"update_test_index\", \"label\": 0, \"vector\": [2.0, 2.0, 2.0]}"
    };
    
    ToolResult update_result = {0};
    int exec_result = execute_vector_db_update_vector_tool_call(&update_call, &update_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(update_result.result);
    TEST_ASSERT_EQUAL_INT(1, update_result.success);
    TEST_ASSERT_TRUE(strstr(update_result.result, "\"success\": true") != NULL);
    
    free(update_result.tool_call_id);
    free(update_result.result);
}

void test_vector_db_delete_vector(void) {
    // Create index and add a vector
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"delete_test_index\", \"dimension\": 3}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    ToolCall add_call = {
        .id = "add_id",
        .name = "vector_db_add_vector",
        .arguments = "{\"index_name\": \"delete_test_index\", \"vector\": [1.0, 2.0, 3.0]}"
    };
    
    ToolResult add_result = {0};
    execute_vector_db_add_vector_tool_call(&add_call, &add_result);
    free(add_result.tool_call_id);
    free(add_result.result);
    
    // Delete the vector
    ToolCall delete_call = {
        .id = "delete_id",
        .name = "vector_db_delete_vector",
        .arguments = "{\"index_name\": \"delete_test_index\", \"label\": 0}"
    };
    
    ToolResult delete_result = {0};
    int exec_result = execute_vector_db_delete_vector_tool_call(&delete_call, &delete_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(delete_result.result);
    TEST_ASSERT_EQUAL_INT(1, delete_result.success);
    TEST_ASSERT_TRUE(strstr(delete_result.result, "\"success\": true") != NULL);
    
    free(delete_result.tool_call_id);
    free(delete_result.result);
}

void test_vector_db_delete_index(void) {
    // Create an index
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"to_delete_index\", \"dimension\": 64}"
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    // Delete the index
    ToolCall delete_call = {
        .id = "delete_id",
        .name = "vector_db_delete_index",
        .arguments = "{\"index_name\": \"to_delete_index\"}"
    };
    
    ToolResult delete_result = {0};
    int exec_result = execute_vector_db_delete_index_tool_call(&delete_call, &delete_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(delete_result.result);
    TEST_ASSERT_EQUAL_INT(1, delete_result.success);
    TEST_ASSERT_TRUE(strstr(delete_result.result, "\"success\": true") != NULL);
    
    free(delete_result.tool_call_id);
    free(delete_result.result);
}

void test_vector_db_error_handling(void) {
    // Test missing required parameters
    ToolCall bad_call = {
        .id = "bad_id",
        .name = "vector_db_create_index",
        .arguments = "{\"dimension\": 128}"  // Missing index_name
    };
    
    ToolResult bad_result = {0};
    int exec_result = execute_vector_db_create_index_tool_call(&bad_call, &bad_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(bad_result.result);
    TEST_ASSERT_EQUAL_INT(0, bad_result.success);
    TEST_ASSERT_TRUE(strstr(bad_result.result, "\"success\": false") != NULL);
    
    free(bad_result.tool_call_id);
    free(bad_result.result);
    
    // Test invalid dimension
    ToolCall invalid_dim_call = {
        .id = "invalid_dim_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"test\", \"dimension\": 0}"
    };
    
    ToolResult invalid_dim_result = {0};
    exec_result = execute_vector_db_create_index_tool_call(&invalid_dim_call, &invalid_dim_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(invalid_dim_result.result);
    TEST_ASSERT_EQUAL_INT(0, invalid_dim_result.success);
    TEST_ASSERT_TRUE(strstr(invalid_dim_result.result, "\"success\": false") != NULL);
    
    free(invalid_dim_result.tool_call_id);
    free(invalid_dim_result.result);
}

void test_vector_db_add_text(void) {
    // First create an index with appropriate dimension for text embeddings
    ToolCall create_call = {
        .id = "create_id",
        .name = "vector_db_create_index",
        .arguments = "{\"index_name\": \"text_test_index\", \"dimension\": 1536}"  // text-embedding-3-small dimension
    };
    
    ToolResult create_result = {0};
    execute_vector_db_create_index_tool_call(&create_call, &create_result);
    TEST_ASSERT_EQUAL_INT(1, create_result.success);
    free(create_result.tool_call_id);
    free(create_result.result);
    
    // Now add text to the index
    ToolCall add_text_call = {
        .id = "add_text_id",
        .name = "vector_db_add_text",
        .arguments = "{\"index_name\": \"text_test_index\", \"text\": \"This is a test document about machine learning and AI.\"}"
    };
    
    ToolResult add_result = {0};
    int exec_result = execute_vector_db_add_text_tool_call(&add_text_call, &add_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(add_result.result);
    TEST_ASSERT_EQUAL_INT(1, add_result.success);
    TEST_ASSERT_TRUE(strstr(add_result.result, "\"success\": true") != NULL);
    TEST_ASSERT_TRUE(strstr(add_result.result, "\"id\": 0") != NULL);
    TEST_ASSERT_TRUE(strstr(add_result.result, "Text embedded and stored successfully") != NULL);
    
    free(add_result.tool_call_id);
    free(add_result.result);
    
    // Clean up
    ToolCall delete_call = {
        .id = "delete_id",
        .name = "vector_db_delete_index",
        .arguments = "{\"index_name\": \"text_test_index\"}"
    };
    
    ToolResult delete_result = {0};
    execute_vector_db_delete_index_tool_call(&delete_call, &delete_result);
    free(delete_result.tool_call_id);
    free(delete_result.result);
}

void test_vector_db_add_text_error_handling(void) {
    // Test missing required parameters
    ToolCall bad_call = {
        .id = "bad_id",
        .name = "vector_db_add_text",
        .arguments = "{\"index_name\": \"test_index\"}"  // Missing text
    };
    
    ToolResult bad_result = {0};
    int exec_result = execute_vector_db_add_text_tool_call(&bad_call, &bad_result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(bad_result.result);
    TEST_ASSERT_EQUAL_INT(0, bad_result.success);
    TEST_ASSERT_TRUE(strstr(bad_result.result, "\"success\": false") != NULL);
    TEST_ASSERT_TRUE(strstr(bad_result.result, "Missing required parameters") != NULL);
    
    free(bad_result.tool_call_id);
    free(bad_result.result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_register_vector_db_tool);
    RUN_TEST(test_vector_db_create_index);
    RUN_TEST(test_vector_db_create_index_with_all_params);
    RUN_TEST(test_vector_db_list_indices);
    RUN_TEST(test_vector_db_add_vector);
    RUN_TEST(test_vector_db_get_vector);
    RUN_TEST(test_vector_db_search);
    RUN_TEST(test_vector_db_update_vector);
    RUN_TEST(test_vector_db_delete_vector);
    RUN_TEST(test_vector_db_delete_index);
    RUN_TEST(test_vector_db_error_handling);
    RUN_TEST(test_vector_db_add_text);
    RUN_TEST(test_vector_db_add_text_error_handling);
    
    return UNITY_END();
}