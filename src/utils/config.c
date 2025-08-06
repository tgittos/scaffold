#include "config.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

static ralph_config_t *g_config = NULL;

static void config_set_defaults(ralph_config_t *config)
{
    if (!config) return;
    
    // Set default values
    config->api_url = strdup("https://api.openai.com/v1/chat/completions");
    config->model = strdup("o4-mini-2025-04-16");
    config->context_window = 8192;
    config->max_tokens = -1;
    
    // Initialize other pointers to NULL
    config->api_key = NULL;
    config->anthropic_api_key = NULL;
    config->openai_api_key = NULL;
    config->openai_api_url = NULL;
    config->embedding_model = NULL;
    config->system_prompt = NULL;
    config->config_file_path = NULL;
    config->config_loaded = false;
}


static void config_update_api_key_selection(ralph_config_t *config)
{
    if (!config) return;
    
    // Set main API key based on URL
    if (config->api_url && strstr(config->api_url, "api.anthropic.com") != NULL) {
        if (config->anthropic_api_key) {
            free(config->api_key);
            config->api_key = strdup(config->anthropic_api_key);
        }
    } else {
        if (config->openai_api_key) {
            free(config->api_key);
            config->api_key = strdup(config->openai_api_key);
        }
    }
}

static char* get_user_config_dir(void)
{
    const char *home = getenv("HOME");
    if (!home) return NULL;
    
    char *config_dir = malloc(strlen(home) + strlen("/.local/ralph") + 1);
    if (!config_dir) return NULL;
    
    sprintf(config_dir, "%s/.local/ralph", home);
    return config_dir;
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
    
    // Try to generate in current directory first
    const char *local_path = "./ralph.config.json";
    
    // Check if we can write to current directory
    if (access(".", W_OK) == 0) {
        // Pre-fill OpenAI API key from environment if available
        const char *env_openai_key = getenv("OPENAI_API_KEY");
        if (env_openai_key) {
            free(g_config->openai_api_key);
            g_config->openai_api_key = strdup(env_openai_key);
            config_update_api_key_selection(g_config);
        }
        
        if (config_save_to_file(local_path) == 0) {
            fprintf(stderr, "[Config] Created %s (add API keys to enable)\n\n", local_path);
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
                
                // Pre-fill OpenAI API key from environment if available
                const char *env_openai_key = getenv("OPENAI_API_KEY");
                if (env_openai_key) {
                    free(g_config->openai_api_key);
                    g_config->openai_api_key = strdup(env_openai_key);
                    config_update_api_key_selection(g_config);
                }
                
                if (config_save_to_file(user_config_file) == 0) {
                    fprintf(stderr, "[Config] Created %s (add API keys to enable)\n\n", user_config_file);
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
        return -1; // File doesn't exist or can't be opened
    }
    
    // Read file content
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
    
    // Parse JSON and update configuration
    cJSON *json = cJSON_Parse(json_content);
    free(json_content);
    
    if (!json) {
        fprintf(stderr, "Error: Invalid JSON in config file %s\n", filepath);
        return -1;
    }
    
    // Update configuration from JSON
    cJSON *item;
    
    item = cJSON_GetObjectItem(json, "api_url");
    if (cJSON_IsString(item)) {
        free(g_config->api_url);
        g_config->api_url = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "model");
    if (cJSON_IsString(item)) {
        free(g_config->model);
        g_config->model = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "anthropic_api_key");
    if (cJSON_IsString(item)) {
        free(g_config->anthropic_api_key);
        g_config->anthropic_api_key = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "openai_api_key");
    if (cJSON_IsString(item)) {
        free(g_config->openai_api_key);
        g_config->openai_api_key = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "openai_api_url");
    if (cJSON_IsString(item)) {
        free(g_config->openai_api_url);
        g_config->openai_api_url = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "embedding_model");
    if (cJSON_IsString(item)) {
        free(g_config->embedding_model);
        g_config->embedding_model = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "system_prompt");
    if (cJSON_IsString(item)) {
        free(g_config->system_prompt);
        g_config->system_prompt = strdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "context_window");
    if (cJSON_IsNumber(item) && item->valueint > 0) {
        g_config->context_window = item->valueint;
    }
    
    item = cJSON_GetObjectItem(json, "max_tokens");
    if (cJSON_IsNumber(item)) {
        g_config->max_tokens = item->valueint;
    }
    
    // Update main API key based on URL
    config_update_api_key_selection(g_config);
    
    cJSON_Delete(json);
    g_config->config_loaded = true;
    
    // Store the config file path
    free(g_config->config_file_path);
    g_config->config_file_path = strdup(filepath);
    
    return 0;
}

int config_save_to_file(const char *filepath)
{
    if (!g_config || !filepath) return -1;
    
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;
    
    // Add configuration values to JSON
    if (g_config->api_url)
        cJSON_AddStringToObject(json, "api_url", g_config->api_url);
    if (g_config->model)
        cJSON_AddStringToObject(json, "model", g_config->model);
    if (g_config->anthropic_api_key)
        cJSON_AddStringToObject(json, "anthropic_api_key", g_config->anthropic_api_key);
    
    // Always include openai_api_key field, even if empty
    cJSON_AddStringToObject(json, "openai_api_key", g_config->openai_api_key ? g_config->openai_api_key : "");
    
    if (g_config->openai_api_url)
        cJSON_AddStringToObject(json, "openai_api_url", g_config->openai_api_url);
    if (g_config->embedding_model)
        cJSON_AddStringToObject(json, "embedding_model", g_config->embedding_model);
    if (g_config->system_prompt)
        cJSON_AddStringToObject(json, "system_prompt", g_config->system_prompt);
    
    cJSON_AddNumberToObject(json, "context_window", g_config->context_window);
    cJSON_AddNumberToObject(json, "max_tokens", g_config->max_tokens);
    
    // Convert to string
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (!json_string) return -1;
    
    // Write to file
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
        // Already initialized
        return 0;
    }
    
    g_config = malloc(sizeof(ralph_config_t));
    if (!g_config) return -1;
    
    memset(g_config, 0, sizeof(ralph_config_t));
    
    // Set defaults
    config_set_defaults(g_config);
    
    // Try to load configuration in priority order:
    // 1. ./ralph.config.json (local override)
    // 2. ~/.local/ralph/config.json (user config)
    
    bool config_loaded = false;
    
    // Try local config file first
    if (access("./ralph.config.json", R_OK) == 0) {
        if (config_load_from_file("./ralph.config.json") == 0) {
            config_loaded = true;
        }
    }
    
    // Try user config directory if local config not found
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
    
    // If no config was loaded, generate a default config file
    if (!config_loaded) {
        config_generate_default_file();
    }
    
    
    return 0;
}

ralph_config_t* config_get(void)
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
    
    if (strcmp(key, "api_url") == 0) {
        free(g_config->api_url);
        g_config->api_url = value ? strdup(value) : NULL;
        need_api_key_update = true;
    } else if (strcmp(key, "model") == 0) {
        free(g_config->model);
        g_config->model = value ? strdup(value) : NULL;
    } else if (strcmp(key, "anthropic_api_key") == 0) {
        free(g_config->anthropic_api_key);
        g_config->anthropic_api_key = value ? strdup(value) : NULL;
        need_api_key_update = true;
    } else if (strcmp(key, "openai_api_key") == 0) {
        free(g_config->openai_api_key);
        g_config->openai_api_key = value ? strdup(value) : NULL;
        need_api_key_update = true;
    } else if (strcmp(key, "openai_api_url") == 0) {
        free(g_config->openai_api_url);
        g_config->openai_api_url = value ? strdup(value) : NULL;
    } else if (strcmp(key, "embedding_model") == 0) {
        free(g_config->embedding_model);
        g_config->embedding_model = value ? strdup(value) : NULL;
    } else if (strcmp(key, "system_prompt") == 0) {
        free(g_config->system_prompt);
        g_config->system_prompt = value ? strdup(value) : NULL;
    } else if (strcmp(key, "context_window") == 0) {
        if (value) {
            int parsed = atoi(value);
            if (parsed > 0) g_config->context_window = parsed;
        }
    } else if (strcmp(key, "max_tokens") == 0) {
        if (value) {
            int parsed = atoi(value);
            g_config->max_tokens = parsed;
        }
    } else {
        return -1; // Unknown key
    }
    
    // Update API key selection if needed
    if (need_api_key_update) {
        config_update_api_key_selection(g_config);
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
    }
    
    return default_value;
}