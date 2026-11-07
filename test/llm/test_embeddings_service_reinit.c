/**
 * Tests for embeddings service reinitialization (own binary).
 *
 * Reinitialization re-reads the environment, re-probes the model and
 * caches the new dimension. It lives in a separate test binary because
 * the reinitialize probe is a later request within one process, and the
 * in-process HTTP client misbehaves on later loopback connections when
 * many mock-server requests have already been made in the same process
 * (see test_embeddings_service, which covers the first-request cases).
 */

#include "../unity/unity.h"
#include "llm/embeddings_service.h"
#include "util/app_home.h"
#include "util/config.h"
#include "../mock_api_server.h"
#include "../test_fs_utils.h"
#include <cJSON.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REINIT_MOCK_PORT 18899

static char g_test_home[256];
static char* saved_openai_api_key = NULL;
static char* saved_openai_api_url = NULL;
static char* saved_embedding_api_url = NULL;
static char* saved_embedding_model = NULL;

static MockAPIServer mock_server;
static MockAPIResponse mock_responses[1];

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
    cJSON_AddItemToObject(item, "embedding", arr);
    cJSON_AddItemToArray(data, item);
    cJSON_AddItemToObject(resp, "data", data);
    cJSON_AddStringToObject(resp, "object", "list");
    cJSON_AddStringToObject(resp, "model", "mock");
    cJSON_AddNumberToObject(resp, "usage", 0);

    char* str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return str;
}

/* Set the local-provider environment for a fresh 384-dim model. */
static void set_local_model_env(void) {
    setenv("OPENAI_API_KEY", "mock-test-key", 1);
    setenv("EMBEDDING_API_URL", "http://127.0.0.1:18899/v1/embeddings", 1);
    setenv("EMBEDDING_MODEL", "mock-model-384", 1);
}

void setUp(void) {
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/test_embeddings_service_reinit_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_test_home));
    app_home_init(g_test_home);
    config_init();

    memset(&mock_server, 0, sizeof(mock_server));
    mock_server.port = REINIT_MOCK_PORT;

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
}

void tearDown(void) {
    if (saved_openai_api_key) { setenv("OPENAI_API_KEY", saved_openai_api_key, 1); free(saved_openai_api_key); saved_openai_api_key = NULL; } else { unsetenv("OPENAI_API_KEY"); }
    if (saved_openai_api_url) { setenv("OPENAI_API_URL", saved_openai_api_url, 1); free(saved_openai_api_url); saved_openai_api_url = NULL; } else { unsetenv("OPENAI_API_URL"); }
    if (saved_embedding_api_url) { setenv("EMBEDDING_API_URL", saved_embedding_api_url, 1); free(saved_embedding_api_url); saved_embedding_api_url = NULL; } else { unsetenv("EMBEDDING_API_URL"); }
    if (saved_embedding_model) { setenv("EMBEDDING_MODEL", saved_embedding_model, 1); free(saved_embedding_model); saved_embedding_model = NULL; } else { unsetenv("EMBEDDING_MODEL"); }

    mock_api_server_stop(&mock_server);
    config_cleanup();
    rmdir_recursive(g_test_home);
    app_home_cleanup();
}

void test_reinitialize_reprobes_dimension(void) {
    mock_responses[0] = mock_openai_embeddings_dynamic(dim_embedding_callback, (void*)(uintptr_t)384);
    set_local_model_env();

    embeddings_service_t* service = embeddings_service_create();
    TEST_ASSERT_NOT_NULL(service);
    TEST_ASSERT_EQUAL_UINT(384, embeddings_service_get_dimension(service));

    /* Same server now serves a different model name (still 384 dims) */
    mock_responses[0] = mock_openai_embeddings_dynamic(dim_embedding_callback, (void*)(uintptr_t)384);
    setenv("EMBEDDING_MODEL", "mock-model-384b", 1);
    usleep(200 * 1000);

    TEST_ASSERT_EQUAL_INT(0, embeddings_service_reinitialize(service));
    TEST_ASSERT_EQUAL_INT(1, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(384, embeddings_service_get_dimension(service));

    /* Server goes away: reinitialize must disable the feature */
    MockAPIResponse fail = {0};
    fail.endpoint = "/v1/embeddings";
    fail.method = "POST";
    fail.should_fail = 1;
    mock_responses[0] = fail;
    usleep(200 * 1000);

    TEST_ASSERT_EQUAL_INT(0, embeddings_service_reinitialize(service));
    TEST_ASSERT_EQUAL_INT(0, embeddings_service_is_configured(service));
    TEST_ASSERT_EQUAL_UINT(EMBEDDINGS_DIM_DEFAULT, embeddings_service_get_dimension(service));

    embeddings_service_destroy(service);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_reinitialize_reprobes_dimension);
    return UNITY_END();
}
