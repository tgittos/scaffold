#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

struct HTTPResponse {
    char *data;
    size_t size;
};

int http_post(const char *url, const char *post_data, struct HTTPResponse *response);
void cleanup_response(struct HTTPResponse *response);

#endif /* HTTP_CLIENT_H */