#ifndef OPENAI_LOGIN_H
#define OPENAI_LOGIN_H

#include <stddef.h>

/*
 * Interactive OpenAI OAuth login.
 * Opens browser (or prints URL in headless mode), waits for callback,
 * exchanges code for tokens, stores encrypted in oauth2.db.
 *
 * @param db_path  Path to the oauth2.db database
 * @param headless If non-zero, skip browser launch and print URL only
 * @return 0 on success, -1 on error
 */
int openai_login(const char *db_path, int headless);

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
