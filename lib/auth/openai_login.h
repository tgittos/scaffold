#ifndef OPENAI_LOGIN_H
#define OPENAI_LOGIN_H

#include <stddef.h>

/*
 * Clean up the persistent OAuth2 store (if any).
 * Call at shutdown or register via atexit().
 */
void openai_auth_cleanup(void);

/*
 * Interactive OpenAI OAuth login.
 * Opens browser (or prints URL in headless mode), waits for callback,
 * exchanges code for tokens, stores encrypted in oauth2.db.
 * Auto-detects headless environments (SSH, Codespaces, no DISPLAY).
 *
 * @param db_path  Path to the oauth2.db database
 * @return 0 on success, -1 on error
 */
int openai_login(const char *db_path);

/*
 * Check if a valid OpenAI OAuth token exists.
 *
 * @param db_path Path to the oauth2.db database
 * @return 1 if logged in, 0 if not
 */
int openai_is_logged_in(const char *db_path);

/*
 * Remove stored OpenAI OAuth tokens.
 *
 * @param db_path Path to the oauth2.db database
 * @return 0 on success, -1 on error
 */
int openai_logout(const char *db_path);

/*
 * Credential provider callback for llm_client_set_credential_provider().
 * Retrieves a fresh access token from the persistent store, auto-refreshing
 * if expired. user_data must be a persistent db_path string.
 *
 * @param key_buf      Output buffer for the access token
 * @param key_buf_len  Size of key_buf
 * @param user_data    Pointer to the db_path string
 * @return 0 on success, -1 on error
 */
int openai_refresh_credential(char *key_buf, size_t key_buf_len, void *user_data);

/*
 * Get access token and account ID for Codex API requests.
 * Handles auto-refresh with token rotation.
 *
 * @param db_path      Path to the oauth2.db database
 * @param access_token Output buffer for the access token
 * @param at_len       Size of access_token buffer
 * @param account_id   Output buffer for the chatgpt_account_id
 * @param aid_len      Size of account_id buffer
 * @return 0 on success, -1 on error
 */
int openai_get_codex_credentials(const char *db_path,
                                  char *access_token, size_t at_len,
                                  char *account_id, size_t aid_len);

#endif /* OPENAI_LOGIN_H */
