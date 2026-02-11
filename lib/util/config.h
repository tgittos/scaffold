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

    int api_max_retries;
    int api_retry_delay_ms;
    float api_backoff_factor;

    int max_subagents;
    int subagent_timeout;       // seconds

    bool enable_streaming;
    bool json_output_mode;
    bool check_updates;

    char *model_simple;         // "simple" tier model ID
    char *model_standard;       // "standard" tier model ID
    char *model_high;           // "high" tier model ID

    char *config_file_path;
    bool config_loaded;
} agent_config_t;

/**
 * Initialize the configuration system
 * Loads configuration with increasing priority:
 * 1. Built-in defaults (lowest)
 * 2. $RALPH_HOME/config.json (default ~/.local/ralph/config.json)
 * 3. Environment variables (highest, override config file values)
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
agent_config_t* config_get(void);

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

/**
 * Get a boolean configuration value by key
 *
 * @param key Configuration key
 * @param default_value Default value if key not found
 * @return Configuration value or default_value
 */
bool config_get_bool(const char *key, bool default_value);

/**
 * Resolve a model name: if it matches a tier name ("simple", "standard", "high"),
 * return the mapped model ID; otherwise return the input as-is.
 *
 * @param name Tier name or raw model ID
 * @return Model ID string (points to config internals or to name itself — do NOT free)
 */
const char* config_resolve_model(const char *name);

#endif /* CONFIG_H */
