#include "../../lib/network/http_client.h"
#include <stdlib.h>
#include <string.h>

static const char *mock_response_json = NULL;
static int mock_error_code = 0;
static long mock_http_status = 200;

void mock_http_set_response(const char *json, int error_code) {
    mock_response_json = json;
    mock_error_code = error_code;
    mock_http_status = (error_code == 0) ? 200 : 0;
}

const struct HTTPConfig DEFAULT_HTTP_CONFIG = {
    .timeout_seconds = 120,
    .connect_timeout_seconds = 30,
    .follow_redirects = 1,
    .max_redirects = 5
};

int http_get_with_config(const char *url, const char **headers,
                        const struct HTTPConfig *config, struct HTTPResponse *response)
{
    (void)url;
    (void)headers;
    (void)config;

    if (response == NULL) return -1;

    response->data = NULL;
    response->size = 0;
    response->content_type = NULL;
    response->http_status = 0;

    if (mock_error_code != 0) {
        return mock_error_code;
    }

    if (mock_response_json != NULL) {
        response->data = strdup(mock_response_json);
        response->size = strlen(mock_response_json);
        response->http_status = mock_http_status;
        response->content_type = strdup("application/json");
        return 0;
    }

    return -1;
}

int http_get(const char *url, struct HTTPResponse *response)
{
    return http_get_with_config(url, NULL, &DEFAULT_HTTP_CONFIG, response);
}

int http_download_file(const char *url, const char **headers,
                       const struct HTTPConfig *config,
                       const char *dest_path, size_t *bytes_written)
{
    (void)url;
    (void)headers;
    (void)config;
    (void)dest_path;
    if (bytes_written) *bytes_written = 0;
    return mock_error_code;
}

void cleanup_response(struct HTTPResponse *response)
{
    if (response == NULL) return;
    free(response->data);
    response->data = NULL;
    response->size = 0;
    free(response->content_type);
    response->content_type = NULL;
    response->http_status = 0;
}
