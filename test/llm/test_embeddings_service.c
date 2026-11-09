/**
 * Tests for embeddings service dimension probing.
 *
 * The service must resolve the embedding dimension from the actual model
 * (via a probe request to the configured endpoint), not from a static
 * per-provider default. If the probe fails, the feature is disabled.
 *
 * Uses the real embeddings service + providers + HTTP client (from the
 * libagent archive) pointed at a local mock API server.
 */

#include "../unity/unity.h"
#include "llm/embeddings_service.h"
#include "llm/embeddings.h"
#include "db/vector_db.h"
#include "db/document_store.h"
#include "tools/memory_tool.h"
#include "tools/tools_system.h"
#include "services/services.h"
#include "util/app_home.h"
#include "util/config.h"
#include "../mock_api_server.h"
#include "../test_fs_utils.h"
#include <cJSON.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void hnswlib_clear_all(void);

#define EMB_MOCK_PORT 18897

static char g_test_home[256];
static char* saved_openai_api_key = NULL;
static char* saved_openai_api_url = NULL;
static char* saved_embedding_api_url = NULL;
static char* saved_embedding_model = NULL;

static MockAPIServer mock_server;
static MockAPIResponse mock_responses[1];
static Services* test_services = NULL;

/* ------------------------------------------------------------------ */
/* Deterministic dim-parameterized embedding generator                */
/* ------------------------------------------------------------------ */

static unsigned long hash_string_local(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static float pseudo_random_local(unsigned long* seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return (float)(*seed) / (float)0x7fffffff;
}

/*
 * Mock server callback. user_data encodes the output dimension.
 * The same text always yields the same vector, so cosine search can
 * match identical texts exactly.
 */
static char* dim_embedding_callback(const char* request_body, void* user_data) {
    size_t dim = (size_t)(uintptr_t)user_data;
    if (dim == 0) {
        dim = 384;
    }

    if (request_body == NULL) {
        return NULL;
    }

    cJSON* root = cJSON_Parse(request_body);
    if (root == NULL) {
        return NULL;
    }

    cJSON* input = cJSON_GetObjectItem(root, "input");
    if (input == NULL || !cJSON_IsString(input) || input->valuestring == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    /* Hash the text before deleting root: input is a child of it. */
    unsigned long seed = hash_string_local(input->valuestring);
    cJSON_Delete(root);

    cJSON* resp = cJSON_CreateObject();
    cJSON* data = cJSON_CreateArray();
    cJSON* item = cJSON_CreateObject();
    cJSON* arr = cJSON_CreateArray();
    if (resp == NULL || data == NULL || item == NULL || arr == NULL) {
        cJSON_Delete(resp);
        cJSON_Delete(data);
        cJSON_Delete(item);
        cJSON_Delete(arr);
        return NULL;
    }

    for (size_t i = 0; i < dim; i++) {
        float v = (pseudo_random_local(&seed) * 2.0f) - 1.0f;
        if (!cJSON_AddItemToArray(arr, cJSON_CreateNumber(v))) {
            cJSON_Delete(resp);
            return NULL;
        }
    }

    cJSON_AddStringToObject(item, "object", "embedding");
    cJSON_AddNumberToObject(item, "index", 0);
    cJSON_AddItemToObject(item, "embedding", arr);   /* takes ownership */
    cJSON_AddItemToArray(data, item);                /* takes ownership */
    cJSON_AddItemToObject(resp, "data", data);       /* takes ownership */
    cJSON_AddStringToObject(resp, "object", "list");
    cJSON_AddStringToObject(resp, "model", "mock");

    char* response = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    return response;
}

/* ------------------------------------------------------------------ */
/* Unity lifecycle                                                    */
/* ------------------------------------------------------------------ */

void setUp(void) {
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/test_embeddings_service_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_test_home));
    app_home_init(g_test_home);
    config_init();

    memset(&mock_server, 0, sizeof(mock_server));
    mock_server.port = EMB_MOCK_PORT;

    mock_responses[0] = mock_openai_embeddings_dynamic(dim_embedding_callback, (void*)(uintptr_t)384);
    mock_server.responses = mock_responses;
    mock_server.response_count = 1;

    mock_api_server_start(&mock_server);
    mock_api_server_wait_ready(&mock_server, 2000);

    const char* k = getenv("OPENAI_API_KEY");
    const char* ou = getenv("OPENAI_API_URL");
    const char* eu = getenv("EMBEDDING_API_URL");
    const char* em = getenv("EMBEDDING_MODEL");
    saved_openai_api_key = k ? strdup(k) : NULL;
    saved_openai_api_url = ou ? strdup(ou) : NULL;
    saved_embedding_api_url = eu ? strdup(eu) : NULL;
    saved_embedding_model = em ? strdup(em) : NULL;

    setenv("OPENAI_API_KEY", "mock-test-key", 1);
    unsetenv("EMBEDDING_API_URL");
    unsetenv("EMBEDDING_MODEL");
    unsetenv("OPENAI_API_URL");

    test_services = NULL;
}

void tearDown(void) {
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
    if (saved_embedding_api_url) {
        setenv("EMBEDDING_API_URL", saved_embedding_api_url, 1);
        free(saved_embedding_api_url);
        saved_embedding_api_url = NULL;
    } else {
        unsetenv("EMBEDDING_API_URL");
    }
    if (saved_embedding_model) {
        setenv("EMBEDDING_MODEL", saved_embedding_model, 1);
        free(saved_embedding_model);
        saved_embedding_model = NULL;
    } else {
        unsetenv("EMBEDDING_MODEL");
    }

    if (test_services != NULL) {
        services_destroy(test_services);
        test_services = NULL;
    }

    mock_api_server_stop(&mock_server);
    config_cleanup();
    hnswlib_clear_all();
    rmdir_recursive(g_test_home);
    app_home_cleanup();
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void set_local_model_env(void) {
    setenv("EMBEDDING_API_URL",
           "http://127.0.0.1:18897/v1/embeddings", 1);
    setenv("EMBEDDING_MODEL", "mock-model-384", 1);
}

void test_probe_success_resolves_model_dimension(void) {
    /* Local-provider URL; the model serves 384-dim vectors */
    mock_responses[0] = mock_openai_embeddings_dynamic(dim_embedding_callback, (void*)(uintptr_t)384);
    set_local_model_env();

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(384, embeddings_service_get_dimension(service));

    /* get_vector must also produce the model's real dimension */
    embedding_vector_t embedding = {0};
    TEST_ASSERT_EQUAL_INT(0, embeddings_service_get_vector(service, "hello", &embedding));
    TEST_ASSERT_EQUAL_UINT(384, embedding.dimension);
    embeddings_service_free_embedding(&embedding);

    embeddings_service_destroy(service);
}

void test_probe_success_openai_provider(void) {
    /* OpenAI-provider URL (path contains api.openai.com); model serves 1536 */
    mock_responses[0] = mock_openai_embeddings_dynamic(dim_embedding_callback, (void*)(uintptr_t)1536);
    setenv("OPENAI_API_URL",
           "http://127.0.0.1:18897/api.openai.com/v1/embeddings", 1);

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(1536, embeddings_service_get_dimension(service));

    embeddings_service_destroy(service);
}

void test_probe_failure_disables_feature(void) {
    MockAPIResponse fail = {0};
    fail.endpoint = "/v1/embeddings";
    fail.method = "POST";
    fail.should_fail = 1;
    mock_responses[0] = fail;

    set_local_model_env();

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    /* Probe failed: feature disabled, no static-dimension fallback */
    TEST_ASSERT_EQUAL_INT(0, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(EMBEDDINGS_DIM_DEFAULT, embeddings_service_get_dimension(service));

    embedding_vector_t embedding = {0};
    TEST_ASSERT_NOT_EQUAL(0, embeddings_service_get_vector(service, "hello", &embedding));
    TEST_ASSERT_NULL(embeddings_service_text_to_vector(service, "hello"));

    embeddings_service_destroy(service);
}

void test_no_api_key_local_url_configured(void) {
    /* Local servers don't require auth */
    unsetenv("OPENAI_API_KEY");
    set_local_model_env();

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(384, embeddings_service_get_dimension(service));

    embeddings_service_destroy(service);
}

void test_no_api_key_no_url_unconfigured(void) {
    unsetenv("OPENAI_API_KEY");
    config_set("openai_api_key", "");

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    TEST_ASSERT_EQUAL_INT(0, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(EMBEDDINGS_DIM_DEFAULT, embeddings_service_get_dimension(service));

    embeddings_service_destroy(service);
}

void test_config_file_url_and_model_used(void) {
    unsetenv("OPENAI_API_KEY");
    TEST_ASSERT_EQUAL_INT(0, config_set("embedding_api_url", "http://127.0.0.1:18897/v1/embeddings"));
    TEST_ASSERT_EQUAL_INT(0, config_set("embedding_model", "mock-model-384"));

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(384, embeddings_service_get_dimension(service));

    embeddings_service_destroy(service);
}

void test_env_url_overrides_config(void) {
    TEST_ASSERT_EQUAL_INT(0, config_set("embedding_api_url", "http://127.0.0.1:1/v1/embeddings"));
    set_local_model_env();

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);

    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(service));

    embeddings_service_destroy(service);
}

void test_index_dimensions_follow_model_end_to_end(void) {
    /*
     * Full path: services (probe -> 384) -> memory tool ensure_index ->
     * add text -> the stored vector must live in a 384-dim index.
     */
    mock_responses[0] = mock_openai_embeddings_dynamic(dim_embedding_callback, (void*)(uintptr_t)384);
    set_local_model_env();

    test_services = services_create_default();
    TEST_ASSERT_NOT_NULL(test_services);

    embeddings_service_t* embeddings = services_get_embeddings(test_services);
    TEST_ASSERT_NOT_NULL(embeddings);
    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(embeddings));
    TEST_ASSERT_EQUAL_UINT(384, embeddings_service_get_dimension(embeddings));

    ToolRegistry registry = {0};
    init_tool_registry(&registry);
    registry.services = test_services;
    register_memory_tools(&registry);

    ToolCall tool_call = {
        .id = "test_id",
        .name = "remember",
        .arguments = "{\"content\": \"scaffold runs locally with a 384-dimension embedding model\"}"
    };

    ToolResult result = {0};
    int exec_result = execute_remember_tool_call(&tool_call, &result);

    int got_exec = exec_result;
    int got_success = result.success;
    int got_error = (result.result != NULL) ? (strstr(result.result, "error") != NULL) : 1;
    free(result.tool_call_id);
    free(result.result);

    TEST_ASSERT_EQUAL_INT(0, got_exec);
    TEST_ASSERT_EQUAL_INT(1, got_success);
    TEST_ASSERT_EQUAL_INT(0, got_error);

    /* The stored vector must be in the index at the model's dimension */
    document_store_t* store = services_get_document_store(test_services);
    TEST_ASSERT_NOT_NULL(store);
    vector_db_t* db = vector_db_service_get_database(services_get_vector_db(test_services));
    TEST_ASSERT_EQUAL_UINT(1, vector_db_get_index_size(db, "long_term_memory"));

    vector_t* vec = vector_create(384);
    TEST_ASSERT_EQUAL_INT(VECTOR_DB_OK, vector_db_get_vector(db, "long_term_memory", 0, vec));
    TEST_ASSERT_EQUAL_UINT(384, vec->dimension);
    vector_destroy(vec);

    cleanup_tool_registry(&registry);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_probe_success_resolves_model_dimension);
    RUN_TEST(test_probe_success_openai_provider);
    RUN_TEST(test_probe_failure_disables_feature);
    RUN_TEST(test_no_api_key_local_url_configured);
    RUN_TEST(test_no_api_key_no_url_unconfigured);
    RUN_TEST(test_config_file_url_and_model_used);
    RUN_TEST(test_env_url_overrides_config);
    RUN_TEST(test_index_dimensions_follow_model_end_to_end);

    return UNITY_END();
}
