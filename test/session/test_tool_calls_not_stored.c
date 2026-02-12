#include "../unity/unity.h"
#include "session/conversation_tracker.h"
#include "db/document_store.h"
#include "db/vector_db_service.h"
#include "services/services.h"

extern void hnswlib_clear_all(void);
extern void document_store_clear_conversations(document_store_t* store);
#include "llm/embeddings_service.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cJSON.h>
#include "util/app_home.h"
#include "../test_fs_utils.h"
#include "../mock_api_server.h"
#include "../mock_embeddings.h"
#include "../mock_embeddings_server.h"

static char g_test_home[256];
static Services* g_test_services = NULL;
static MockAPIServer mock_server;
static MockAPIResponse mock_responses[1];
static char* saved_api_key = NULL;
static char* saved_api_url = NULL;

void setUp(void) {
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/test_tcns_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_test_home));
    app_home_init(g_test_home);

    mock_embeddings_init_test_groups();

    memset(&mock_server, 0, sizeof(mock_server));
    mock_server.port = 18893;
    mock_responses[0] = mock_embeddings_server_response();
    mock_server.responses = mock_responses;
    mock_server.response_count = 1;
    mock_api_server_start(&mock_server);
    mock_api_server_wait_ready(&mock_server, 2000);

    // Save and set env vars so embeddings_service_create() picks them up
    const char* key = getenv("OPENAI_API_KEY");
    const char* url = getenv("EMBEDDING_API_URL");
    saved_api_key = key ? strdup(key) : NULL;
    saved_api_url = url ? strdup(url) : NULL;
    setenv("OPENAI_API_KEY", "mock-test-key", 1);
    setenv("EMBEDDING_API_URL", "http://127.0.0.1:18893/v1/embeddings", 1);

    g_test_services = services_create_empty();
    if (g_test_services) {
        g_test_services->vector_db = vector_db_service_create();
        g_test_services->embeddings = embeddings_service_create();
        document_store_set_services(g_test_services);
        g_test_services->document_store = document_store_create(NULL);
    }
    conversation_tracker_set_services(g_test_services);

    document_store_clear_conversations(services_get_document_store(g_test_services));
}

void tearDown(void) {
    document_store_clear_conversations(services_get_document_store(g_test_services));

    conversation_tracker_set_services(NULL);
    document_store_set_services(NULL);

    if (g_test_services) {
        services_destroy(g_test_services);
        g_test_services = NULL;
    }

    hnswlib_clear_all();

    mock_api_server_stop(&mock_server);
    mock_embeddings_cleanup();

    // Restore env vars
    if (saved_api_key) {
        setenv("OPENAI_API_KEY", saved_api_key, 1);
        free(saved_api_key);
        saved_api_key = NULL;
    } else {
        unsetenv("OPENAI_API_KEY");
    }
    if (saved_api_url) {
        setenv("EMBEDDING_API_URL", saved_api_url, 1);
        free(saved_api_url);
        saved_api_url = NULL;
    } else {
        unsetenv("EMBEDDING_API_URL");
    }

    rmdir_recursive(g_test_home);
    app_home_cleanup();
}

/**
 * Test that assistant messages with tool_calls don't have tool_calls stored in memory.
 *
 * When ralph stores conversation history to the vector database (for long-term memory),
 * only the user messages and assistant's final response content should be stored.
 * Tool calls are ephemeral implementation details that don't need to be remembered.
 */
void test_assistant_tool_calls_not_stored_in_vector_db(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add a user message
    append_conversation_message(&history, "user", "What's the weather like in London?");

    // Add an assistant message in GPT format with tool_calls
    // This is what format_model_assistant_tool_message creates for GPT models
    const char* assistant_with_tool_calls =
        "{\"role\": \"assistant\", \"content\": null, \"tool_calls\": [{\"id\": \"call_abc123\", \"type\": \"function\", \"function\": {\"name\": \"get_weather\", \"arguments\": \"{\\\"location\\\": \\\"London\\\"}\"}}]}";

    append_conversation_message(&history, "assistant", assistant_with_tool_calls);

    // Give some time for processing
    usleep(100000); // 100ms

    // Add a final assistant response (no tool_calls)
    append_conversation_message(&history, "assistant", "The weather in London is sunny and 22 degrees.");

    // Clear in-memory history and reload from vector database
    cleanup_conversation_history(&history);

    // Give some time for the database to sync
    usleep(100000); // 100ms

    ConversationHistory loaded;
    init_conversation_history(&loaded);

    int result = load_conversation_history(&loaded);
    TEST_ASSERT_EQUAL_INT(0, result);

    printf("Loaded %zu messages from conversation history\n", loaded.count);

    // Check each assistant message - none should contain tool_calls
    for (size_t i = 0; i < loaded.count; i++) {
        printf("Message %zu: role=%s\n", i, loaded.data[i].role);

        if (strcmp(loaded.data[i].role, "assistant") == 0) {
            const char* content = loaded.data[i].content;
            printf("  content: %s\n", content);

            // The stored content should NOT contain "tool_calls"
            TEST_ASSERT_NULL_MESSAGE(
                strstr(content, "tool_calls"),
                "Assistant message in memory should not contain tool_calls"
            );

            // If it's JSON, parse it and verify no tool_calls key
            cJSON* json = cJSON_Parse(content);
            if (json) {
                cJSON* tool_calls = cJSON_GetObjectItem(json, "tool_calls");
                TEST_ASSERT_NULL_MESSAGE(
                    tool_calls,
                    "Assistant message JSON should not have tool_calls key"
                );
                cJSON_Delete(json);
            }
        }
    }

    cleanup_conversation_history(&loaded);
}

/**
 * Test that assistant messages with actual content plus tool_calls
 * only store the content, not the tool_calls.
 */
void test_assistant_content_with_tool_calls_stores_only_content(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add a user message
    append_conversation_message(&history, "user", "Help me check the weather");

    // Add an assistant message with both content AND tool_calls
    const char* assistant_with_content_and_tools =
        "{\"role\": \"assistant\", \"content\": \"Let me check the weather for you.\", \"tool_calls\": [{\"id\": \"call_xyz789\", \"type\": \"function\", \"function\": {\"name\": \"get_weather\", \"arguments\": \"{\\\"location\\\": \\\"Paris\\\"}\"}}]}";

    append_conversation_message(&history, "assistant", assistant_with_content_and_tools);

    usleep(100000); // 100ms

    // Clear and reload
    cleanup_conversation_history(&history);

    usleep(100000); // 100ms

    ConversationHistory loaded;
    init_conversation_history(&loaded);

    int result = load_conversation_history(&loaded);
    TEST_ASSERT_EQUAL_INT(0, result);

    printf("Loaded %zu messages\n", loaded.count);

    // Find the assistant message and verify only content is stored
    int found_assistant = 0;
    for (size_t i = 0; i < loaded.count; i++) {
        if (strcmp(loaded.data[i].role, "assistant") == 0) {
            found_assistant = 1;
            const char* content = loaded.data[i].content;
            printf("Assistant content: %s\n", content);

            // Should NOT contain tool_calls
            TEST_ASSERT_NULL_MESSAGE(
                strstr(content, "tool_calls"),
                "Stored assistant message should not contain tool_calls"
            );

            // SHOULD contain the actual message content
            TEST_ASSERT_NOT_NULL_MESSAGE(
                strstr(content, "Let me check the weather"),
                "Stored assistant message should contain the actual content"
            );
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(found_assistant, "Should have found an assistant message");

    cleanup_conversation_history(&loaded);
}

/**
 * Test that assistant messages with only tool_calls (null content)
 * are not stored at all in the vector database.
 */
void test_assistant_with_only_tool_calls_not_stored(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add a user message
    append_conversation_message(&history, "user", "Check the temperature");

    usleep(100000);

    // Add an assistant message with ONLY tool_calls (null/empty content)
    // These are pure tool invocation messages with no user-facing content
    const char* tool_only_message =
        "{\"role\": \"assistant\", \"content\": null, \"tool_calls\": [{\"id\": \"call_temp123\", \"type\": \"function\", \"function\": {\"name\": \"get_temp\", \"arguments\": \"{}\"}}]}";

    append_conversation_message(&history, "assistant", tool_only_message);

    usleep(100000);

    // Add a meaningful final response
    append_conversation_message(&history, "assistant", "The temperature is 20 degrees.");

    // Clear and reload
    cleanup_conversation_history(&history);

    usleep(100000);

    ConversationHistory loaded;
    init_conversation_history(&loaded);

    int result = load_conversation_history(&loaded);
    TEST_ASSERT_EQUAL_INT(0, result);

    printf("Loaded %zu messages\n", loaded.count);

    // Count meaningful assistant messages (should only be the final one)
    int meaningful_assistant_count = 0;
    for (size_t i = 0; i < loaded.count; i++) {
        if (strcmp(loaded.data[i].role, "assistant") == 0) {
            const char* content = loaded.data[i].content;
            printf("Assistant message: %s\n", content);

            // Should not be a tool-only message
            if (strstr(content, "tool_calls") == NULL) {
                meaningful_assistant_count++;
            }
        }
    }

    // We should only have 1 meaningful assistant message (the final response)
    // The tool-only message should not be stored
    printf("Found %d meaningful assistant messages\n", meaningful_assistant_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, meaningful_assistant_count,
        "Only meaningful assistant responses should be stored, not tool-only messages");

    cleanup_conversation_history(&loaded);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_assistant_tool_calls_not_stored_in_vector_db);
    RUN_TEST(test_assistant_content_with_tool_calls_stores_only_content);
    RUN_TEST(test_assistant_with_only_tool_calls_not_stored);
    return UNITY_END();
}
