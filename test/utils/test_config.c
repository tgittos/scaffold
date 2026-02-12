#include "../../test/unity/unity.h"
#include "util/config.h"
#include "util/app_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *g_test_home = "/tmp/test_config_home";

static void remove_test_home(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", g_test_home);
    unlink(path);
    rmdir(g_test_home);
}

void setUp(void) {
    config_cleanup();
    app_home_cleanup();

    remove_test_home();
    mkdir(g_test_home, 0755);
    app_home_init(g_test_home);

    unlink("test_config.json");

    unsetenv("API_URL");
    unsetenv("MODEL");
    unsetenv("OPENAI_API_KEY");
    unsetenv("ANTHROPIC_API_KEY");
    unsetenv("OPENAI_API_URL");
    unsetenv("EMBEDDING_MODEL");
    unsetenv("CONTEXT_WINDOW");
    unsetenv("MAX_TOKENS");
}

void tearDown(void) {
    config_cleanup();
    unlink("test_config.json");
    remove_test_home();
    app_home_cleanup();
}

void test_config_init_with_defaults(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_NOT_NULL(config->api_url);
    TEST_ASSERT_NOT_NULL(config->model);
    TEST_ASSERT_EQUAL_STRING("https://api.openai.com/v1/chat/completions", config->api_url);
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", config->model);
    TEST_ASSERT_EQUAL(8192, config->context_window);
    TEST_ASSERT_EQUAL(-1, config->max_tokens);
    TEST_ASSERT_TRUE(config->enable_streaming);  // Default should be true
}

void test_config_init_with_anthropic_config(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", g_test_home);
    FILE *test_file = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(test_file);

    const char *json_content =
        "{\n"
        "  \"api_url\": \"https://api.anthropic.com/v1/messages\",\n"
        "  \"model\": \"claude-3-sonnet-20240229\",\n"
        "  \"anthropic_api_key\": \"test-key\",\n"
        "  \"context_window\": 4096,\n"
        "  \"max_tokens\": 1000\n"
        "}\n";

    fprintf(test_file, "%s", json_content);
    fclose(test_file);

    TEST_ASSERT_EQUAL(0, config_init());

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING("https://api.anthropic.com/v1/messages", config->api_url);
    TEST_ASSERT_EQUAL_STRING("claude-3-sonnet-20240229", config->model);
    TEST_ASSERT_EQUAL_STRING("test-key", config->api_key);
    TEST_ASSERT_EQUAL_STRING("test-key", config->anthropic_api_key);
    TEST_ASSERT_EQUAL(4096, config->context_window);
    TEST_ASSERT_EQUAL(1000, config->max_tokens);
}

void test_config_load_from_json_file(void) {
    // Create a test JSON config file
    FILE *test_file = fopen("test_config.json", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    
    const char *json_content = 
        "{\n"
        "  \"api_url\": \"https://api.example.com/v1/chat\",\n"
        "  \"model\": \"test-model\",\n"
        "  \"openai_api_key\": \"test-openai-key\",\n"
        "  \"embedding_model\": \"text-embedding-test\",\n"
        "  \"context_window\": 2048,\n"
        "  \"max_tokens\": 500\n"
        "}\n";
    
    fwrite(json_content, 1, strlen(json_content), test_file);
    fclose(test_file);
    
    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_load_from_file("test_config.json"));
    
    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING("https://api.example.com/v1/chat", config->api_url);
    TEST_ASSERT_EQUAL_STRING("test-model", config->model);
    TEST_ASSERT_EQUAL_STRING("test-openai-key", config->openai_api_key);
    TEST_ASSERT_EQUAL_STRING("test-openai-key", config->api_key); // Should be set from openai_api_key
    TEST_ASSERT_EQUAL_STRING("text-embedding-test", config->embedding_model);
    TEST_ASSERT_EQUAL(2048, config->context_window);
    TEST_ASSERT_EQUAL(500, config->max_tokens);
}

void test_config_save_to_json_file(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Set some configuration values
    TEST_ASSERT_EQUAL(0, config_set("api_url", "https://api.example.com/v1/chat"));
    TEST_ASSERT_EQUAL(0, config_set("model", "test-model"));
    TEST_ASSERT_EQUAL(0, config_set("openai_api_key", "test-key"));
    TEST_ASSERT_EQUAL(0, config_set("context_window", "2048"));
    
    // Save to file
    TEST_ASSERT_EQUAL(0, config_save_to_file("test_config.json"));
    
    // Verify file exists
    FILE *test_file = fopen("test_config.json", "r");
    TEST_ASSERT_NOT_NULL(test_file);
    fclose(test_file);
    
    // Load configuration from file to verify saved content
    config_cleanup();
    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_load_from_file("test_config.json"));
    
    agent_config_t *config = config_get();
    TEST_ASSERT_EQUAL_STRING("https://api.example.com/v1/chat", config->api_url);
    TEST_ASSERT_EQUAL_STRING("test-model", config->model);
    TEST_ASSERT_EQUAL_STRING("test-key", config->openai_api_key);
    TEST_ASSERT_EQUAL(2048, config->context_window);
}

void test_config_local_override_priority(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", g_test_home);
    FILE *config_file = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(config_file);

    const char *json =
        "{\n"
        "  \"api_url\": \"https://local.example.com/v1/chat\",\n"
        "  \"model\": \"local-model\"\n"
        "}\n";

    fwrite(json, 1, strlen(json), config_file);
    fclose(config_file);

    TEST_ASSERT_EQUAL(0, config_init());

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING("https://local.example.com/v1/chat", config->api_url);
    TEST_ASSERT_EQUAL_STRING("local-model", config->model);
}

void test_config_get_string(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    TEST_ASSERT_EQUAL(0, config_set("api_url", "https://test.example.com"));
    TEST_ASSERT_EQUAL(0, config_set("model", "test-model"));
    
    TEST_ASSERT_EQUAL_STRING("https://test.example.com", config_get_string("api_url"));
    TEST_ASSERT_EQUAL_STRING("test-model", config_get_string("model"));
    TEST_ASSERT_NULL(config_get_string("nonexistent_key"));
}

void test_config_get_int(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    TEST_ASSERT_EQUAL(0, config_set("context_window", "4096"));
    TEST_ASSERT_EQUAL(0, config_set("max_tokens", "1000"));
    
    TEST_ASSERT_EQUAL(4096, config_get_int("context_window", -1));
    TEST_ASSERT_EQUAL(1000, config_get_int("max_tokens", -1));
    TEST_ASSERT_EQUAL(999, config_get_int("nonexistent_key", 999)); // Should return default
}

void test_config_set_invalid_key(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Should return -1 for invalid key
    TEST_ASSERT_EQUAL(-1, config_set("invalid_key", "value"));
}

void test_config_set_api_max_retries(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    agent_config_t *config = config_get();
    int original = config->api_max_retries;

    // Set to zero (disable retries)
    TEST_ASSERT_EQUAL(0, config_set("api_max_retries", "0"));
    TEST_ASSERT_EQUAL(0, config->api_max_retries);

    // Set to positive value
    TEST_ASSERT_EQUAL(0, config_set("api_max_retries", "5"));
    TEST_ASSERT_EQUAL(5, config->api_max_retries);

    // Negative value should be rejected (unchanged)
    TEST_ASSERT_EQUAL(0, config_set("api_max_retries", "-1"));
    TEST_ASSERT_EQUAL(5, config->api_max_retries);

    // NULL value should not crash
    TEST_ASSERT_EQUAL(0, config_set("api_max_retries", NULL));

    // Restore original
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", original);
    config_set("api_max_retries", buf);
}

void test_config_anthropic_api_key_selection(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Set up Anthropic URL and key
    TEST_ASSERT_EQUAL(0, config_set("api_url", "https://api.anthropic.com/v1/messages"));
    TEST_ASSERT_EQUAL(0, config_set("anthropic_api_key", "anthropic-key"));
    TEST_ASSERT_EQUAL(0, config_set("openai_api_key", "openai-key"));
    
    agent_config_t *config = config_get();
    // Should select anthropic key for anthropic URL
    TEST_ASSERT_EQUAL_STRING("anthropic-key", config->api_key);
}

void test_config_openai_api_key_selection(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Set up OpenAI URL and key
    TEST_ASSERT_EQUAL(0, config_set("api_url", "https://api.openai.com/v1/chat/completions"));
    TEST_ASSERT_EQUAL(0, config_set("anthropic_api_key", "anthropic-key"));
    TEST_ASSERT_EQUAL(0, config_set("openai_api_key", "openai-key"));
    
    agent_config_t *config = config_get();
    // Should select openai key for openai URL
    TEST_ASSERT_EQUAL_STRING("openai-key", config->api_key);
}

void test_config_load_nonexistent_file(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Should return -1 for nonexistent file
    TEST_ASSERT_EQUAL(-1, config_load_from_file("nonexistent_file.json"));
}

void test_config_save_invalid_path(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    // Should return -1 for invalid path
    TEST_ASSERT_EQUAL(-1, config_save_to_file("/invalid/path/config.json"));
}

void test_config_enable_streaming_default(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    // Default should be true
    TEST_ASSERT_TRUE(config_get_bool("enable_streaming", false));

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_TRUE(config->enable_streaming);
}

void test_config_enable_streaming_load_from_file(void) {
    // Create a test JSON config file with streaming disabled
    FILE *test_file = fopen("test_config.json", "w");
    TEST_ASSERT_NOT_NULL(test_file);

    const char *json_content =
        "{\n"
        "  \"api_url\": \"https://api.example.com/v1/chat\",\n"
        "  \"model\": \"test-model\",\n"
        "  \"enable_streaming\": false\n"
        "}\n";

    fwrite(json_content, 1, strlen(json_content), test_file);
    fclose(test_file);

    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_load_from_file("test_config.json"));

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_FALSE(config->enable_streaming);
    TEST_ASSERT_FALSE(config_get_bool("enable_streaming", true));
}

void test_config_enable_streaming_save_to_file(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    // Modify streaming setting
    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    config->enable_streaming = false;

    // Save to file
    TEST_ASSERT_EQUAL(0, config_save_to_file("test_config.json"));

    // Reload and verify
    config_cleanup();
    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_load_from_file("test_config.json"));

    config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_FALSE(config->enable_streaming);
}

void test_config_get_bool_nonexistent_key(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    // Should return default for nonexistent key
    TEST_ASSERT_TRUE(config_get_bool("nonexistent_key", true));
    TEST_ASSERT_FALSE(config_get_bool("nonexistent_key", false));
}

void test_config_model_tier_defaults(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING("o4-mini", config->model_simple);
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", config->model_standard);
    TEST_ASSERT_EQUAL_STRING("gpt-5.2-2025-12-11", config->model_high);

    TEST_ASSERT_EQUAL_STRING("o4-mini", config_get_string("model_simple"));
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", config_get_string("model_standard"));
    TEST_ASSERT_EQUAL_STRING("gpt-5.2-2025-12-11", config_get_string("model_high"));
}

void test_config_model_tiers_load_from_file(void) {
    FILE *test_file = fopen("test_config.json", "w");
    TEST_ASSERT_NOT_NULL(test_file);

    const char *json_content =
        "{\n"
        "  \"model\": \"gpt-5-mini-2025-08-07\",\n"
        "  \"models\": {\n"
        "    \"simple\": \"custom-small\",\n"
        "    \"standard\": \"custom-medium\",\n"
        "    \"high\": \"custom-large\"\n"
        "  }\n"
        "}\n";

    fwrite(json_content, 1, strlen(json_content), test_file);
    fclose(test_file);

    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_load_from_file("test_config.json"));

    agent_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING("custom-small", config->model_simple);
    TEST_ASSERT_EQUAL_STRING("custom-medium", config->model_standard);
    TEST_ASSERT_EQUAL_STRING("custom-large", config->model_high);
}

void test_config_model_tiers_save_to_file(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    TEST_ASSERT_EQUAL(0, config_set("model_simple", "my-simple"));
    TEST_ASSERT_EQUAL(0, config_set("model_standard", "my-standard"));
    TEST_ASSERT_EQUAL(0, config_set("model_high", "my-high"));

    TEST_ASSERT_EQUAL(0, config_save_to_file("test_config.json"));

    config_cleanup();
    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_load_from_file("test_config.json"));

    agent_config_t *config = config_get();
    TEST_ASSERT_EQUAL_STRING("my-simple", config->model_simple);
    TEST_ASSERT_EQUAL_STRING("my-standard", config->model_standard);
    TEST_ASSERT_EQUAL_STRING("my-high", config->model_high);
}

void test_config_resolve_model_tier_names(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    TEST_ASSERT_EQUAL_STRING("o4-mini", config_resolve_model("simple"));
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", config_resolve_model("standard"));
    TEST_ASSERT_EQUAL_STRING("gpt-5.2-2025-12-11", config_resolve_model("high"));
}

void test_config_resolve_model_raw_id(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    TEST_ASSERT_EQUAL_STRING("gpt-4o", config_resolve_model("gpt-4o"));
    TEST_ASSERT_EQUAL_STRING("claude-3-sonnet", config_resolve_model("claude-3-sonnet"));
}

void test_config_resolve_model_null(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    TEST_ASSERT_NULL(config_resolve_model(NULL));
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_config_init_with_defaults);
    RUN_TEST(test_config_init_with_anthropic_config);
    RUN_TEST(test_config_load_from_json_file);
    RUN_TEST(test_config_save_to_json_file);
    RUN_TEST(test_config_local_override_priority);
    RUN_TEST(test_config_get_string);
    RUN_TEST(test_config_get_int);
    RUN_TEST(test_config_set_invalid_key);
    RUN_TEST(test_config_set_api_max_retries);
    RUN_TEST(test_config_anthropic_api_key_selection);
    RUN_TEST(test_config_openai_api_key_selection);
    RUN_TEST(test_config_load_nonexistent_file);
    RUN_TEST(test_config_save_invalid_path);
    RUN_TEST(test_config_enable_streaming_default);
    RUN_TEST(test_config_enable_streaming_load_from_file);
    RUN_TEST(test_config_enable_streaming_save_to_file);
    RUN_TEST(test_config_get_bool_nonexistent_key);
    RUN_TEST(test_config_model_tier_defaults);
    RUN_TEST(test_config_model_tiers_load_from_file);
    RUN_TEST(test_config_model_tiers_save_to_file);
    RUN_TEST(test_config_resolve_model_tier_names);
    RUN_TEST(test_config_resolve_model_raw_id);
    RUN_TEST(test_config_resolve_model_null);

    return UNITY_END();
}
