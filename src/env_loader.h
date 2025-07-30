#ifndef ENV_LOADER_H
#define ENV_LOADER_H

/**
 * Load environment variables from a .env file
 * 
 * @param filepath Path to the .env file (e.g., ".env")
 * @return 0 on success, -1 on error
 */
int load_env_file(const char *filepath);

#endif /* ENV_LOADER_H */