#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

typedef struct {
    char *api_url;
    char *model;
    char *api_key;
    char *anthropic_api_key;
    char *openai_api_key;
    char *openai_api_url;
    char *embedding_api_url;
    char *embedding_model;
    char *system_prompt;
    int context_window;
    int max_tokens;

    // API retry configuration
    int api_max_retries;        // Default: 3
    int api_retry_delay_ms;     // Default: 1000 (1 second)
    float api_backoff_factor;   // Default: 2.0

    // Configuration file paths
    char *config_file_path;
    bool config_loaded;
} ralph_config_t;

/**
 * Initialize the configuration system
 * This will attempt to load configuration from:
 * 1. ./ralph.config.json (if present)
 * 2. ~/.local/ralph/config.json (if ~/.local/ralph exists)
 * 3. Environment variables (as fallback)
 * 4. Built-in defaults
 * 
 * @return 0 on success, -1 on error
 */
int config_init(void);

/**
 * Get the global configuration instance
 * Must call config_init() first
 * 
 * @return Pointer to global config, or NULL if not initialized
 */
ralph_config_t* config_get(void);

/**
 * Clean up configuration resources
 */
void config_cleanup(void);

/**
 * Load configuration from a JSON file
 * 
 * @param filepath Path to JSON configuration file
 * @return 0 on success, -1 on error
 */
int config_load_from_file(const char *filepath);

/**
 * Save current configuration to a JSON file
 * 
 * @param filepath Path to save JSON configuration file
 * @return 0 on success, -1 on error
 */
int config_save_to_file(const char *filepath);

/**
 * Set a configuration value by key
 * 
 * @param key Configuration key
 * @param value Configuration value
 * @return 0 on success, -1 on error
 */
int config_set(const char *key, const char *value);

/**
 * Get a configuration value by key
 * 
 * @param key Configuration key
 * @return Configuration value or NULL if not found
 */
const char* config_get_string(const char *key);

/**
 * Get an integer configuration value by key
 *
 * @param key Configuration key
 * @param default_value Default value if key not found
 * @return Configuration value or default_value
 */
int config_get_int(const char *key, int default_value);

/**
 * Get a float configuration value by key
 *
 * @param key Configuration key
 * @param default_value Default value if key not found
 * @return Configuration value or default_value
 */
float config_get_float(const char *key, float default_value);

#endif /* CONFIG_H */