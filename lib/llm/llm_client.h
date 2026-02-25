#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include "../network/http_client.h"

/* Credential provider callback: called before each request to refresh the
 * API key if needed.  Returns 0 on success (key_buf updated), -1 on error. */
typedef int (*llm_credential_provider_fn)(char *key_buf, size_t key_buf_len,
                                          void *user_data);

void llm_client_set_credential_provider(llm_credential_provider_fn fn,
                                         void *user_data);

/* Invoke the registered credential provider.  Returns 0 on success. */
int llm_client_refresh_credential(char *key_buf, size_t key_buf_len);

int llm_client_init(void);
void llm_client_cleanup(void);

int llm_client_send(const char* api_url, const char** headers,
                    const char* payload, struct HTTPResponse* response);

int llm_client_send_streaming(const char* api_url, const char** headers,
                              const char* payload,
                              struct StreamingHTTPConfig* config);

#endif /* LLM_CLIENT_H */
