#include "../unity/unity.h"
#include "session/conversation_tracker.h"
#include "db/document_store.h"
#include "db/vector_db_service.h"
#include "util/config.h"
#include "util/ralph_home.h"
#include "../mock_api_server.h"
#include "../mock_embeddings.h"
#include "../mock_embeddings_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static MockAPIServer mock_server;
static MockAPIResponse mock_responses[1];

void setUp(void) {
    // Initialize ralph home directory (required for document store)
    ralph_home_init(NULL);

    // Initialize mock embeddings
    mock_embeddings_init_test_groups();

    // Assign test texts to semantic groups for realistic search results
    mock_embeddings_assign_to_group("Tell me about quantum physics", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("Quantum physics is the study of matter at atomic scales", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("quantum", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("What about classical mechanics?", MOCK_GROUP_CLASSICAL);
    mock_embeddings_assign_to_group("Classical mechanics deals with macroscopic objects", MOCK_GROUP_CLASSICAL);

    // Start mock embeddings server
    memset(&mock_server, 0, sizeof(mock_server));
    mock_server.port = 18891;  // Different port from other tests
    mock_responses[0] = mock_embeddings_server_response();
    mock_server.responses = mock_responses;
    mock_server.response_count = 1;
    mock_api_server_start(&mock_server);
    mock_api_server_wait_ready(&mock_server, 2000);

    // Initialize config and override embedding API URL to use mock server
    config_init();
    config_set("embedding_api_url", "http://127.0.0.1:18891/v1/embeddings");
}

void tearDown(void) {
    // Clear conversation data after each test to prevent interference
    document_store_clear_conversations();

    // Stop mock server
    mock_api_server_stop(&mock_server);

    // Cleanup mock embeddings
    mock_embeddings_cleanup();

    // Cleanup config
    config_cleanup();

    // Cleanup ralph home
    ralph_home_cleanup();
}

void test_conversation_stored_in_vector_db(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add messages
    int result1 = append_conversation_message(&history, "user", "Hello from vector DB test");
    int result2 = append_conversation_message(&history, "assistant", "Hello! This response is stored in vector DB");
    
    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(0, result2);
    TEST_ASSERT_EQUAL(2, history.count);
    
    // Give the vector DB time to process
    usleep(100000); // 100ms delay
    
    cleanup_conversation_history(&history);
    
    // Now load conversation from vector DB
    ConversationHistory loaded_history;
    init_conversation_history(&loaded_history);
    
    int load_result = load_conversation_history(&loaded_history);
    TEST_ASSERT_EQUAL(0, load_result);
    
    // Should have loaded the messages from vector DB
    TEST_ASSERT_GREATER_OR_EQUAL(2, loaded_history.count);
    
    // Check that our messages are in the loaded history
    int found_user_msg = 0;
    int found_assistant_msg = 0;
    
    for (size_t i = 0; i < loaded_history.count; i++) {
        if (strcmp(loaded_history.data[i].role, "user") == 0 &&
            strstr(loaded_history.data[i].content, "Hello from vector DB test") != NULL) {
            found_user_msg = 1;
        }
        if (strcmp(loaded_history.data[i].role, "assistant") == 0 &&
            strstr(loaded_history.data[i].content, "stored in vector DB") != NULL) {
            found_assistant_msg = 1;
        }
    }
    
    TEST_ASSERT_TRUE(found_user_msg);
    TEST_ASSERT_TRUE(found_assistant_msg);
    
    cleanup_conversation_history(&loaded_history);
    
    // Clear conversation data to prevent test interference
    document_store_clear_conversations();
}

void test_extended_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add some messages
    append_conversation_message(&history, "user", "First message");
    append_conversation_message(&history, "assistant", "First response");
    append_conversation_message(&history, "user", "Second message");
    append_conversation_message(&history, "assistant", "Second response");
    
    cleanup_conversation_history(&history);
    
    // Load extended history
    ConversationHistory extended_history;
    init_conversation_history(&extended_history);
    
    int result = load_extended_conversation_history(&extended_history, 7, 100);
    TEST_ASSERT_EQUAL(0, result);
    
    // Should have loaded messages
    TEST_ASSERT_GREATER_OR_EQUAL(4, extended_history.count);
    
    cleanup_conversation_history(&extended_history);
    
    // Clear conversation data to prevent test interference
    document_store_clear_conversations();
}

void test_search_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add messages with specific keywords
    append_conversation_message(&history, "user", "Tell me about quantum physics");
    append_conversation_message(&history, "assistant", "Quantum physics is the study of matter at atomic scales");
    append_conversation_message(&history, "user", "What about classical mechanics?");
    append_conversation_message(&history, "assistant", "Classical mechanics deals with macroscopic objects");
    
    cleanup_conversation_history(&history);
    
    // Search for quantum-related messages
    ConversationHistory* search_results = search_conversation_history("quantum", 10);
    
    if (search_results != NULL) {
        // Should find at least the quantum-related messages
        TEST_ASSERT_GREATER_OR_EQUAL(1, search_results->count);
        
        // Check that results contain quantum
        int found_quantum = 0;
        for (size_t i = 0; i < search_results->count; i++) {
            if (strstr(search_results->data[i].content, "quantum") != NULL ||
                strstr(search_results->data[i].content, "Quantum") != NULL) {
                found_quantum = 1;
                break;
            }
        }
        TEST_ASSERT_TRUE(found_quantum);
        
        cleanup_conversation_history(search_results);
        free(search_results);
    }
    
    // Clear conversation data to prevent test interference
    document_store_clear_conversations();
}

void test_sliding_window_retrieval(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add many messages to test sliding window
    for (int i = 0; i < 30; i++) {
        char msg[100] = {0};
        snprintf(msg, sizeof(msg), "Message %d", i);
        append_conversation_message(&history, i % 2 == 0 ? "user" : "assistant", msg);
    }
    
    cleanup_conversation_history(&history);
    
    // Load with sliding window (should get most recent messages)
    ConversationHistory windowed_history;
    init_conversation_history(&windowed_history);
    
    int result = load_conversation_history(&windowed_history);
    TEST_ASSERT_EQUAL(0, result);
    
    // Should have loaded up to SLIDING_WINDOW_SIZE messages (20)
    TEST_ASSERT_LESS_OR_EQUAL(20, windowed_history.count);
    
    cleanup_conversation_history(&windowed_history);
    
    // Clear conversation data to prevent test interference
    document_store_clear_conversations();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_conversation_stored_in_vector_db);
    RUN_TEST(test_extended_conversation_history);
    RUN_TEST(test_search_conversation_history);
    RUN_TEST(test_sliding_window_retrieval);
    
    return UNITY_END();
}