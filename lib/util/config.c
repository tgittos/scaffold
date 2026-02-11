#include "config.h"
#include "debug_output.h"
#include "ralph_home.h"
#include <defaults.h>
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
    config->model_simple = NULL;
    config->model_standard = NULL;
    config->model_high = NULL;

    config->api_url = strdup(DEFAULT_API_URL);
    if (!config->api_url) return -1;

    config->model = strdup(DEFAULT_MODEL);
    if (!config->model) return -1;

    config->context_window = DEFAULT_CONTEXT_WINDOW;
    config->max_tokens = DEFAULT_MAX_TOKENS;

    config->api_max_retries = DEFAULT_API_MAX_RETRIES;
    config->api_retry_delay_ms = DEFAULT_API_RETRY_DELAY_MS;
    config->api_backoff_factor = DEFAULT_API_BACKOFF_FACTOR;

    config->max_subagents = DEFAULT_MAX_SUBAGENTS;
    config->subagent_timeout = DEFAULT_SUBAGENT_TIMEOUT;

    config->enable_streaming = DEFAULT_ENABLE_STREAMING;
    config->json_output_mode = DEFAULT_JSON_OUTPUT_MODE;
    config->check_updates = DEFAULT_CHECK_UPDATES;

    config->model_simple = strdup(DEFAULT_MODEL_SIMPLE);
    if (!config->model_simple) return -1;

    config->model_standard = strdup(DEFAULT_MODEL_STANDARD);
    if (!config->model_standard) return -1;

    config->model_high = strdup(DEFAULT_MODEL_HIGH);
    if (!config->model_high) return -1;

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

    if (ralph_home_ensure_exists() != 0) return;

    char *config_file = ralph_home_path("config.json");
    if (config_file) {
        if (config_save_to_file(config_file) == 0) {
            debug_printf("[Config] Created %s with API keys from environment\n\n", config_file);
        }
        free(config_file);
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

    item = cJSON_GetObjectItem(json, "check_updates");
    if (cJSON_IsBool(item)) {
        g_config->check_updates = cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItem(json, "models");
    if (cJSON_IsObject(item)) {
        cJSON *child;
        child = cJSON_GetObjectItem(item, "simple");
        if (cJSON_IsString(child)) {
            new_val = strdup(child->valuestring);
            if (new_val) { free(g_config->model_simple); g_config->model_simple = new_val; }
        }
        child = cJSON_GetObjectItem(item, "standard");
        if (cJSON_IsString(child)) {
            new_val = strdup(child->valuestring);
            if (new_val) { free(g_config->model_standard); g_config->model_standard = new_val; }
        }
        child = cJSON_GetObjectItem(item, "high");
        if (cJSON_IsString(child)) {
            new_val = strdup(child->valuestring);
            if (new_val) { free(g_config->model_high); g_config->model_high = new_val; }
        }
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
    cJSON_AddBoolToObject(json, "check_updates", g_config->check_updates);

    cJSON *models = cJSON_CreateObject();
    if (models) {
        if (g_config->model_simple)
            cJSON_AddStringToObject(models, "simple", g_config->model_simple);
        if (g_config->model_standard)
            cJSON_AddStringToObject(models, "standard", g_config->model_standard);
        if (g_config->model_high)
            cJSON_AddStringToObject(models, "high", g_config->model_high);
        cJSON_AddItemToObject(json, "models", models);
    }

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

    bool config_loaded = false;

    char *config_file = ralph_home_path("config.json");
    if (config_file) {
        if (access(config_file, R_OK) == 0) {
            if (config_load_from_file(config_file) == 0) {
                config_loaded = true;
            }
        }
        free(config_file);
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
    free(g_config->model_simple);
    free(g_config->model_standard);
    free(g_config->model_high);

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
    } else if (strcmp(key, "model_simple") == 0) {
        free(g_config->model_simple);
        g_config->model_simple = new_val;
    } else if (strcmp(key, "model_standard") == 0) {
        free(g_config->model_standard);
        g_config->model_standard = new_val;
    } else if (strcmp(key, "model_high") == 0) {
        free(g_config->model_high);
        g_config->model_high = new_val;
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
    } else if (strcmp(key, "api_max_retries") == 0) {
        free(new_val);
        if (value) {
            int parsed = atoi(value);
            if (parsed >= 0) g_config->api_max_retries = parsed;
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
    } else if (strcmp(key, "model_simple") == 0) {
        return g_config->model_simple;
    } else if (strcmp(key, "model_standard") == 0) {
        return g_config->model_standard;
    } else if (strcmp(key, "model_high") == 0) {
        return g_config->model_high;
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
    } else if (strcmp(key, "check_updates") == 0) {
        return g_config->check_updates;
    }

    return default_value;
}

const char* config_resolve_model(const char *name)
{
    if (!name || !g_config) return name;

    if (strcmp(name, "simple") == 0 && g_config->model_simple)
        return g_config->model_simple;
    if (strcmp(name, "standard") == 0 && g_config->model_standard)
        return g_config->model_standard;
    if (strcmp(name, "high") == 0 && g_config->model_high)
        return g_config->model_high;

    return name;
}
