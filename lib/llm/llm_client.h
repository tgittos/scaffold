#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include "../network/http_client.h"

int llm_client_init(void);
void llm_client_cleanup(void);

int llm_client_send(const char* api_url, const char* api_key,
                    const char* payload, struct HTTPResponse* response);

int llm_client_send_streaming(const char* api_url, const char* api_key,
                              const char* payload,
                              struct StreamingHTTPConfig* config);

#endif /* LLM_CLIENT_H */
