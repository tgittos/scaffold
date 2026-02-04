#include "config.h"
#include "ralph_home.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

static agent_config_t *g_config = NULL;

static int config_set_defaults(agent_config_t *config)
{
    if (!config) return -1;

    config->api_url = NULL;
    config->model = NULL;
    config->api_key = NULL;
    config->anthropic_api_key = NULL;
    config->openai_api_key = NULL;
    config->openai_api_url = NULL;
    config->embedding_api_url = NULL;
    config->embedding_model = NULL;
    config->system_prompt = NULL;
    config->config_file_path = NULL;
    config->config_loaded = false;

    config->api_url = strdup("https://api.openai.com/v1/chat/completions");
    if (!config->api_url) return -1;

    config->model = strdup("gpt-5-mini-2025-08-07");
    if (!config->model) return -1;

    config->context_window = 8192;
    config->max_tokens = -1;

    config->api_max_retries = 3;
    config->api_retry_delay_ms = 1000;
    config->api_backoff_factor = 2.0f;

    config->max_subagents = 5;
    config->subagent_timeout = 300;

    config->enable_streaming = true;
    config->json_output_mode = false;

    return 0;
}


static int config_update_api_key_selection(agent_config_t *config)
{
    if (!config) return -1;

    // Select the appropriate API key based on which provider the URL points to
    if (config->api_url && strstr(config->api_url, "api.anthropic.com") != NULL) {
        if (config->anthropic_api_key) {
            free(config->api_key);
            config->api_key = strdup(config->anthropic_api_key);
            if (!config->api_key) return -1;
        }
    } else {
        if (config->openai_api_key) {
            free(config->api_key);
            config->api_key = strdup(config->openai_api_key);
            if (!config->api_key) return -1;
        }
    }
    return 0;
}

static char* get_user_config_dir(void)
{
    const char *home = ralph_home_get();
    if (!home) return NULL;

    return strdup(home);
}

static int ensure_directory_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    // Create directory with parents
    char *path_copy = strdup(path);
    if (!path_copy) return -1;

    char *p = path_copy;
    while (*p) {
        if (*p == '/' && p != path_copy) {
            *p = '\0';
            mkdir(path_copy, 0755);
            *p = '/';
        }
        p++;
    }

    int result = mkdir(path_copy, 0755);
    free(path_copy);
    return (result == 0 || errno == EEXIST) ? 0 : -1;
}

static void config_generate_default_file(void)
{
    if (!g_config) return;

    const char *env_openai_key = getenv("OPENAI_API_KEY");
    const char *env_anthropic_key = getenv("ANTHROPIC_API_KEY");

    free(g_config->openai_api_key);
    g_config->openai_api_key = strdup(env_openai_key ? env_openai_key : "");
    if (!g_config->openai_api_key) return;

    free(g_config->anthropic_api_key);
    g_config->anthropic_api_key = strdup(env_anthropic_key ? env_anthropic_key : "");
    if (!g_config->anthropic_api_key) return;

    (void)config_update_api_key_selection(g_config);

    const char *local_path = "./ralph.config.json";

    if (access(".", W_OK) == 0) {
        if (config_save_to_file(local_path) == 0) {
            fprintf(stderr, "[Config] Created %s with API keys from environment\n\n", local_path);
            return;
        }
    }

    // Fall back to user config directory
    char *user_config_dir = get_user_config_dir();
    if (user_config_dir) {
        if (ensure_directory_exists(user_config_dir) == 0) {
            char *user_config_file = malloc(strlen(user_config_dir) + strlen("/config.json") + 1);
            if (user_config_file) {
                sprintf(user_config_file, "%s/config.json", user_config_dir);

                if (config_save_to_file(user_config_file) == 0) {
                    fprintf(stderr, "[Config] Created %s with API keys from environment\n\n", user_config_file);
                }
                free(user_config_file);
            }
        }
        free(user_config_dir);
    }
}


int config_load_from_file(const char *filepath)
{
    if (!g_config || !filepath) return -1;

    FILE *file = fopen(filepath, "r");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return -1;
    }

    char *json_content = malloc(file_size + 1);
    if (!json_content) {
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(json_content, 1, file_size, file);
    json_content[bytes_read] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(json_content);
    free(json_content);

    if (!json) {
        fprintf(stderr, "Error: Invalid JSON in config file %s\n", filepath);
        return -1;
    }

    // strdup failures are non-fatal -- we keep the previous value
    cJSON *item;
    char *new_val;

    item = cJSON_GetObjectItem(json, "api_url");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->api_url);
            g_config->api_url = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "model");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->model);
            g_config->model = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "anthropic_api_key");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->anthropic_api_key);
            g_config->anthropic_api_key = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "openai_api_key");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->openai_api_key);
            g_config->openai_api_key = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "openai_api_url");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->openai_api_url);
            g_config->openai_api_url = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "embedding_api_url");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->embedding_api_url);
            g_config->embedding_api_url = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "embedding_model");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->embedding_model);
            g_config->embedding_model = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "system_prompt");
    if (cJSON_IsString(item)) {
        new_val = strdup(item->valuestring);
        if (new_val) {
            free(g_config->system_prompt);
            g_config->system_prompt = new_val;
        }
    }

    item = cJSON_GetObjectItem(json, "context_window");
    if (cJSON_IsNumber(item) && item->valueint > 0) {
        g_config->context_window = item->valueint;
    }

    item = cJSON_GetObjectItem(json, "max_tokens");
    if (cJSON_IsNumber(item)) {
        g_config->max_tokens = item->valueint;
    }

    item = cJSON_GetObjectItem(json, "api_max_retries");
    if (cJSON_IsNumber(item) && item->valueint >= 0) {
        g_config->api_max_retries = item->valueint;
    }

    item = cJSON_GetObjectItem(json, "api_retry_delay_ms");
    if (cJSON_IsNumber(item) && item->valueint > 0) {
        g_config->api_retry_delay_ms = item->valueint;
    }

    item = cJSON_GetObjectItem(json, "api_backoff_factor");
    if (cJSON_IsNumber(item) && item->valuedouble > 0) {
        g_config->api_backoff_factor = (float)item->valuedouble;
    }

    item = cJSON_GetObjectItem(json, "max_subagents");
    if (cJSON_IsNumber(item) && item->valueint > 0) {
        g_config->max_subagents = item->valueint;
    }

    item = cJSON_GetObjectItem(json, "subagent_timeout");
    if (cJSON_IsNumber(item) && item->valueint > 0) {
        g_config->subagent_timeout = item->valueint;
    }

    item = cJSON_GetObjectItem(json, "enable_streaming");
    if (cJSON_IsBool(item)) {
        g_config->enable_streaming = cJSON_IsTrue(item);
    }

    (void)config_update_api_key_selection(g_config);

    cJSON_Delete(json);
    g_config->config_loaded = true;

    new_val = strdup(filepath);
    if (new_val) {
        free(g_config->config_file_path);
        g_config->config_file_path = new_val;
    }

    return 0;
}

int config_save_to_file(const char *filepath)
{
    if (!g_config || !filepath) return -1;

    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;

    if (g_config->api_url)
        cJSON_AddStringToObject(json, "api_url", g_config->api_url);
    if (g_config->model)
        cJSON_AddStringToObject(json, "model", g_config->model);

    // Both key fields are always written so users see them in the config file
    cJSON_AddStringToObject(json, "anthropic_api_key", g_config->anthropic_api_key ? g_config->anthropic_api_key : "");
    cJSON_AddStringToObject(json, "openai_api_key", g_config->openai_api_key ? g_config->openai_api_key : "");

    if (g_config->openai_api_url)
        cJSON_AddStringToObject(json, "openai_api_url", g_config->openai_api_url);
    if (g_config->embedding_api_url)
        cJSON_AddStringToObject(json, "embedding_api_url", g_config->embedding_api_url);
    if (g_config->embedding_model)
        cJSON_AddStringToObject(json, "embedding_model", g_config->embedding_model);
    if (g_config->system_prompt)
        cJSON_AddStringToObject(json, "system_prompt", g_config->system_prompt);

    cJSON_AddNumberToObject(json, "context_window", g_config->context_window);
    cJSON_AddNumberToObject(json, "max_tokens", g_config->max_tokens);

    cJSON_AddNumberToObject(json, "api_max_retries", g_config->api_max_retries);
    cJSON_AddNumberToObject(json, "api_retry_delay_ms", g_config->api_retry_delay_ms);
    cJSON_AddNumberToObject(json, "api_backoff_factor", (double)g_config->api_backoff_factor);

    cJSON_AddNumberToObject(json, "max_subagents", g_config->max_subagents);
    cJSON_AddNumberToObject(json, "subagent_timeout", g_config->subagent_timeout);

    cJSON_AddBoolToObject(json, "enable_streaming", g_config->enable_streaming);

    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);

    if (!json_string) return -1;

    FILE *file = fopen(filepath, "w");
    if (!file) {
        free(json_string);
        return -1;
    }

    size_t written = fwrite(json_string, 1, strlen(json_string), file);
    fclose(file);
    free(json_string);

    return (written > 0) ? 0 : -1;
}

int config_init(void)
{
    if (g_config) {
        return 0;
    }

    g_config = malloc(sizeof(agent_config_t));
    if (!g_config) return -1;

    memset(g_config, 0, sizeof(agent_config_t));

    if (config_set_defaults(g_config) != 0) {
        config_cleanup();
        return -1;
    }

    // Priority: local override > user config > generated defaults
    bool config_loaded = false;

    if (access("./ralph.config.json", R_OK) == 0) {
        if (config_load_from_file("./ralph.config.json") == 0) {
            config_loaded = true;
        }
    }

    if (!config_loaded) {
        char *user_config_dir = get_user_config_dir();
        if (user_config_dir) {
            char *user_config_file = malloc(strlen(user_config_dir) + strlen("/config.json") + 1);
            if (user_config_file) {
                sprintf(user_config_file, "%s/config.json", user_config_dir);
                if (access(user_config_file, R_OK) == 0) {
                    if (config_load_from_file(user_config_file) == 0) {
                        config_loaded = true;
                    }
                }
                free(user_config_file);
            }
            free(user_config_dir);
        }
    }

    if (!config_loaded) {
        config_generate_default_file();
    }

    // Environment variables take precedence over config file values
    const char *env_openai_key = getenv("OPENAI_API_KEY");
    const char *env_anthropic_key = getenv("ANTHROPIC_API_KEY");

    if (env_openai_key) {
        char *new_key = strdup(env_openai_key);
        if (new_key) {
            free(g_config->openai_api_key);
            g_config->openai_api_key = new_key;
        }
    }

    if (env_anthropic_key) {
        char *new_key = strdup(env_anthropic_key);
        if (new_key) {
            free(g_config->anthropic_api_key);
            g_config->anthropic_api_key = new_key;
        }
    }

    (void)config_update_api_key_selection(g_config);

    return 0;
}

agent_config_t* config_get(void)
{
    return g_config;
}

void config_cleanup(void)
{
    if (!g_config) return;

    free(g_config->api_url);
    free(g_config->model);
    free(g_config->api_key);
    free(g_config->anthropic_api_key);
    free(g_config->openai_api_key);
    free(g_config->openai_api_url);
    free(g_config->embedding_api_url);
    free(g_config->embedding_model);
    free(g_config->system_prompt);
    free(g_config->config_file_path);

    free(g_config);
    g_config = NULL;
}

int config_set(const char *key, const char *value)
{
    if (!g_config || !key) return -1;

    bool need_api_key_update = false;
    char *new_val = NULL;

    if (value) {
        new_val = strdup(value);
        if (!new_val) return -1;
    }

    if (strcmp(key, "api_url") == 0) {
        free(g_config->api_url);
        g_config->api_url = new_val;
        need_api_key_update = true;
    } else if (strcmp(key, "model") == 0) {
        free(g_config->model);
        g_config->model = new_val;
    } else if (strcmp(key, "anthropic_api_key") == 0) {
        free(g_config->anthropic_api_key);
        g_config->anthropic_api_key = new_val;
        need_api_key_update = true;
    } else if (strcmp(key, "openai_api_key") == 0) {
        free(g_config->openai_api_key);
        g_config->openai_api_key = new_val;
        need_api_key_update = true;
    } else if (strcmp(key, "openai_api_url") == 0) {
        free(g_config->openai_api_url);
        g_config->openai_api_url = new_val;
    } else if (strcmp(key, "embedding_api_url") == 0) {
        free(g_config->embedding_api_url);
        g_config->embedding_api_url = new_val;
    } else if (strcmp(key, "embedding_model") == 0) {
        free(g_config->embedding_model);
        g_config->embedding_model = new_val;
    } else if (strcmp(key, "system_prompt") == 0) {
        free(g_config->system_prompt);
        g_config->system_prompt = new_val;
    } else if (strcmp(key, "context_window") == 0) {
        free(new_val);
        if (value) {
            int parsed = atoi(value);
            if (parsed > 0) g_config->context_window = parsed;
        }
    } else if (strcmp(key, "max_tokens") == 0) {
        free(new_val);
        if (value) {
            int parsed = atoi(value);
            g_config->max_tokens = parsed;
        }
    } else {
        free(new_val);
        return -1;
    }

    if (need_api_key_update) {
        (void)config_update_api_key_selection(g_config);
    }

    return 0;
}

const char* config_get_string(const char *key)
{
    if (!g_config || !key) return NULL;

    if (strcmp(key, "api_url") == 0) {
        return g_config->api_url;
    } else if (strcmp(key, "model") == 0) {
        return g_config->model;
    } else if (strcmp(key, "api_key") == 0) {
        return g_config->api_key;
    } else if (strcmp(key, "anthropic_api_key") == 0) {
        return g_config->anthropic_api_key;
    } else if (strcmp(key, "openai_api_key") == 0) {
        return g_config->openai_api_key;
    } else if (strcmp(key, "openai_api_url") == 0) {
        return g_config->openai_api_url;
    } else if (strcmp(key, "embedding_api_url") == 0) {
        return g_config->embedding_api_url;
    } else if (strcmp(key, "embedding_model") == 0) {
        return g_config->embedding_model;
    } else if (strcmp(key, "system_prompt") == 0) {
        return g_config->system_prompt;
    }

    return NULL;
}

int config_get_int(const char *key, int default_value)
{
    if (!g_config || !key) return default_value;

    if (strcmp(key, "context_window") == 0) {
        return g_config->context_window;
    } else if (strcmp(key, "max_tokens") == 0) {
        return g_config->max_tokens;
    } else if (strcmp(key, "api_max_retries") == 0) {
        return g_config->api_max_retries;
    } else if (strcmp(key, "api_retry_delay_ms") == 0) {
        return g_config->api_retry_delay_ms;
    } else if (strcmp(key, "max_subagents") == 0) {
        return g_config->max_subagents;
    } else if (strcmp(key, "subagent_timeout") == 0) {
        return g_config->subagent_timeout;
    }

    return default_value;
}

float config_get_float(const char *key, float default_value)
{
    if (!g_config || !key) return default_value;

    if (strcmp(key, "api_backoff_factor") == 0) {
        return g_config->api_backoff_factor;
    }

    return default_value;
}

bool config_get_bool(const char *key, bool default_value)
{
    if (!g_config || !key) return default_value;

    if (strcmp(key, "enable_streaming") == 0) {
        return g_config->enable_streaming;
    }

    return default_value;
}
