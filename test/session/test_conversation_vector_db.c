#include "../unity/unity.h"
#include "session/conversation_tracker.h"
#include "db/document_store.h"
#include "db/vector_db_service.h"
#include "db/hnswlib_wrapper.h"
#include "services/services.h"
#include "llm/embeddings_service.h"
#include "util/ralph_home.h"
#include "../mock_embeddings.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static Services* g_test_services = NULL;

void setUp(void) {
    ralph_home_init(NULL);

    mock_embeddings_init_test_groups();

    mock_embeddings_assign_to_group("Tell me about quantum physics", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("Quantum physics is the study of matter at atomic scales", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("quantum", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("What about classical mechanics?", MOCK_GROUP_CLASSICAL);
    mock_embeddings_assign_to_group("Classical mechanics deals with macroscopic objects", MOCK_GROUP_CLASSICAL);

    g_test_services = services_create_empty();
    g_test_services->vector_db = vector_db_service_create();
    g_test_services->embeddings = embeddings_service_create();
    document_store_set_services(g_test_services);
    g_test_services->document_store = document_store_create(NULL);
    conversation_tracker_set_services(g_test_services);
}

void tearDown(void) {
    document_store_clear_conversations(services_get_document_store(g_test_services));

    hnswlib_clear_all();

    conversation_tracker_set_services(NULL);
    document_store_set_services(NULL);
    if (g_test_services) {
        services_destroy(g_test_services);
        g_test_services = NULL;
    }

    mock_embeddings_cleanup();
    ralph_home_cleanup();
}

void test_conversation_stored_in_vector_db(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    int result1 = append_conversation_message(&history, "user", "Hello from vector DB test");
    int result2 = append_conversation_message(&history, "assistant", "Hello! This response is stored in vector DB");

    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(0, result2);
    TEST_ASSERT_EQUAL(2, history.count);

    usleep(100000);

    cleanup_conversation_history(&history);

    ConversationHistory loaded_history;
    init_conversation_history(&loaded_history);

    int load_result = load_conversation_history(&loaded_history);
    TEST_ASSERT_EQUAL(0, load_result);

    TEST_ASSERT_GREATER_OR_EQUAL(2, loaded_history.count);

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

    document_store_clear_conversations(services_get_document_store(g_test_services));
}

void test_extended_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    append_conversation_message(&history, "user", "First message");
    append_conversation_message(&history, "assistant", "First response");
    append_conversation_message(&history, "user", "Second message");
    append_conversation_message(&history, "assistant", "Second response");

    cleanup_conversation_history(&history);

    ConversationHistory extended_history;
    init_conversation_history(&extended_history);

    int result = load_extended_conversation_history(&extended_history, 7, 100);
    TEST_ASSERT_EQUAL(0, result);

    TEST_ASSERT_GREATER_OR_EQUAL(4, extended_history.count);

    cleanup_conversation_history(&extended_history);

    document_store_clear_conversations(services_get_document_store(g_test_services));
}

void test_search_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    append_conversation_message(&history, "user", "Tell me about quantum physics");
    append_conversation_message(&history, "assistant", "Quantum physics is the study of matter at atomic scales");
    append_conversation_message(&history, "user", "What about classical mechanics?");
    append_conversation_message(&history, "assistant", "Classical mechanics deals with macroscopic objects");

    cleanup_conversation_history(&history);

    ConversationHistory* search_results = search_conversation_history("quantum", 10);

    if (search_results != NULL) {
        TEST_ASSERT_GREATER_OR_EQUAL(1, search_results->count);

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

    document_store_clear_conversations(services_get_document_store(g_test_services));
}

void test_sliding_window_retrieval(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    for (int i = 0; i < 30; i++) {
        char msg[100] = {0};
        snprintf(msg, sizeof(msg), "Message %d", i);
        append_conversation_message(&history, i % 2 == 0 ? "user" : "assistant", msg);
    }

    cleanup_conversation_history(&history);

    ConversationHistory windowed_history;
    init_conversation_history(&windowed_history);

    int result = load_conversation_history(&windowed_history);
    TEST_ASSERT_EQUAL(0, result);

    TEST_ASSERT_LESS_OR_EQUAL(20, windowed_history.count);

    cleanup_conversation_history(&windowed_history);

    document_store_clear_conversations(services_get_document_store(g_test_services));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_conversation_stored_in_vector_db);
    RUN_TEST(test_extended_conversation_history);
    RUN_TEST(test_search_conversation_history);
    RUN_TEST(test_sliding_window_retrieval);

    return UNITY_END();
}
