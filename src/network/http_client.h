#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

struct HTTPResponse {
    char *data;
    size_t size;
};

struct HTTPConfig {
    long timeout_seconds;
    long connect_timeout_seconds;
    int follow_redirects;
    int max_redirects;
};

// Default configuration
extern const struct HTTPConfig DEFAULT_HTTP_CONFIG;

int http_post(const char *url, const char *post_data, struct HTTPResponse *response);
int http_post_with_headers(const char *url, const char *post_data, const char **headers, struct HTTPResponse *response);
int http_post_with_config(const char *url, const char *post_data, const char **headers, 
                         const struct HTTPConfig *config, struct HTTPResponse *response);

int http_get(const char *url, struct HTTPResponse *response);
int http_get_with_headers(const char *url, const char **headers, struct HTTPResponse *response);
int http_get_with_config(const char *url, const char **headers, 
                        const struct HTTPConfig *config, struct HTTPResponse *response);

void cleanup_response(struct HTTPResponse *response);

#endif /* HTTP_CLIENT_H */