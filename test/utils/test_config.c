#include "../../test/unity/unity.h"
#include "util/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char *saved_env_backup = NULL;
static char *saved_ralph_config_backup = NULL;

void setUp(void) {
    // Clean up any existing config
    config_cleanup();
    
    // Back up existing .env file if it exists
    FILE *env_file = fopen(".env", "r");
    if (env_file) {
        fseek(env_file, 0, SEEK_END);
        long file_size = ftell(env_file);
        fseek(env_file, 0, SEEK_SET);
        
        saved_env_backup = malloc(file_size + 1);
        if (saved_env_backup) {
            fread(saved_env_backup, 1, file_size, env_file);
            saved_env_backup[file_size] = '\0';
        }
        fclose(env_file);
        unlink(".env");  // Remove temporarily
    }
    
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
        unlink("ralph.config.json");  // Remove temporarily
    }
    
    // Clean up test files
    unlink("test_config.json");
    unlink("./ralph.config.json");
    
    // Clear environment variables that might interfere with tests
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
    unlink("./ralph.config.json");
    
    // Restore backed up .env file if it existed
    if (saved_env_backup) {
        FILE *env_file = fopen(".env", "w");
        if (env_file) {
            fwrite(saved_env_backup, 1, strlen(saved_env_backup), env_file);
            fclose(env_file);
        }
        free(saved_env_backup);
        saved_env_backup = NULL;
    }
    
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
}

void test_config_init_with_defaults(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    ralph_config_t *config = config_get();
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
    // Create a test JSON config file with Anthropic settings
    FILE *test_file = fopen("ralph.config.json", "w");
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
    
    ralph_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_STRING("https://api.anthropic.com/v1/messages", config->api_url);
    TEST_ASSERT_EQUAL_STRING("claude-3-sonnet-20240229", config->model);
    TEST_ASSERT_EQUAL_STRING("test-key", config->api_key); // Should be set from anthropic_api_key
    TEST_ASSERT_EQUAL_STRING("test-key", config->anthropic_api_key);
    TEST_ASSERT_EQUAL(4096, config->context_window);
    TEST_ASSERT_EQUAL(1000, config->max_tokens);
    
    // Clean up
    unlink("ralph.config.json");
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
    
    ralph_config_t *config = config_get();
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
    
    ralph_config_t *config = config_get();
    TEST_ASSERT_EQUAL_STRING("https://api.example.com/v1/chat", config->api_url);
    TEST_ASSERT_EQUAL_STRING("test-model", config->model);
    TEST_ASSERT_EQUAL_STRING("test-key", config->openai_api_key);
    TEST_ASSERT_EQUAL(2048, config->context_window);
}

void test_config_local_override_priority(void) {
    // Create a local config file
    FILE *local_file = fopen("./ralph.config.json", "w");
    TEST_ASSERT_NOT_NULL(local_file);
    
    const char *local_json = 
        "{\n"
        "  \"api_url\": \"https://local.example.com/v1/chat\",\n"
        "  \"model\": \"local-model\"\n"
        "}\n";
    
    fwrite(local_json, 1, strlen(local_json), local_file);
    fclose(local_file);
    
    TEST_ASSERT_EQUAL(0, config_init());
    
    ralph_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    // Local config should be loaded
    TEST_ASSERT_EQUAL_STRING("https://local.example.com/v1/chat", config->api_url);
    TEST_ASSERT_EQUAL_STRING("local-model", config->model);
    
    // Clean up
    unlink("./ralph.config.json");
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

void test_config_anthropic_api_key_selection(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Set up Anthropic URL and key
    TEST_ASSERT_EQUAL(0, config_set("api_url", "https://api.anthropic.com/v1/messages"));
    TEST_ASSERT_EQUAL(0, config_set("anthropic_api_key", "anthropic-key"));
    TEST_ASSERT_EQUAL(0, config_set("openai_api_key", "openai-key"));
    
    ralph_config_t *config = config_get();
    // Should select anthropic key for anthropic URL
    TEST_ASSERT_EQUAL_STRING("anthropic-key", config->api_key);
}

void test_config_openai_api_key_selection(void) {
    TEST_ASSERT_EQUAL(0, config_init());
    
    // Set up OpenAI URL and key
    TEST_ASSERT_EQUAL(0, config_set("api_url", "https://api.openai.com/v1/chat/completions"));
    TEST_ASSERT_EQUAL(0, config_set("anthropic_api_key", "anthropic-key"));
    TEST_ASSERT_EQUAL(0, config_set("openai_api_key", "openai-key"));
    
    ralph_config_t *config = config_get();
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

    ralph_config_t *config = config_get();
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

    ralph_config_t *config = config_get();
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_FALSE(config->enable_streaming);
    TEST_ASSERT_FALSE(config_get_bool("enable_streaming", true));
}

void test_config_enable_streaming_save_to_file(void) {
    TEST_ASSERT_EQUAL(0, config_init());

    // Modify streaming setting
    ralph_config_t *config = config_get();
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
    RUN_TEST(test_config_anthropic_api_key_selection);
    RUN_TEST(test_config_openai_api_key_selection);
    RUN_TEST(test_config_load_nonexistent_file);
    RUN_TEST(test_config_save_invalid_path);
    RUN_TEST(test_config_enable_streaming_default);
    RUN_TEST(test_config_enable_streaming_load_from_file);
    RUN_TEST(test_config_enable_streaming_save_to_file);
    RUN_TEST(test_config_get_bool_nonexistent_key);

    return UNITY_END();
}
