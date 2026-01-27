#include "http_client.h"
#include "api_error.h"
#include "embedded_cacert.h"
#include "../utils/config.h"
#include "../utils/debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>

// Uses embedded CA bundle so we don't depend on platform-specific cert stores
static void configure_ssl_certs(CURL *curl) {
    struct curl_blob blob;
    blob.data = (void *)embedded_cacert_data;
    blob.len = embedded_cacert_size;
    blob.flags = CURL_BLOB_NOCOPY;
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
}

const struct HTTPConfig DEFAULT_HTTP_CONFIG = {
    .timeout_seconds = 120,
    .connect_timeout_seconds = 30,
    .follow_redirects = 1,
    .max_redirects = 5
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    struct HTTPResponse *response = (struct HTTPResponse *)userp;
    size_t realsize = size * nmemb;
    char *ptr = realloc(response->data, response->size + realsize + 1);
    
    if (ptr == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for response\n");
        return 0;
    }
    
    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = '\0';
    
    return realsize;
}

struct HeaderData {
    int retry_after_seconds;
};

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    struct HeaderData *header_data = (struct HeaderData *)userdata;
    size_t realsize = size * nitems;

    if (realsize > 13 && strncasecmp(buffer, "Retry-After:", 12) == 0) {
        const char *value = buffer + 12;
        while (*value == ' ' || *value == '\t') value++;

        int seconds = atoi(value);
        if (seconds > 0 && seconds <= 300) {
            header_data->retry_after_seconds = seconds;
        }
    }

    return realsize;
}

static int calculate_retry_delay(int attempt, int base_delay_ms, float backoff_factor)
{
    float multiplier = 1.0f;
    for (int i = 0; i < attempt; i++) {
        multiplier *= backoff_factor;
    }

    int delay = (int)(base_delay_ms * multiplier);

    // Jitter avoids thundering herd on concurrent retries
    if (delay > 0) {
        int jitter = rand() % (delay / 4 + 1);
        delay += jitter;
    }

    if (delay > 60000) {
        delay = 60000;
    }

    return delay;
}

int http_post_with_headers(const char *url, const char *post_data, const char **headers, struct HTTPResponse *response)
{
    return http_post_with_config(url, post_data, headers, &DEFAULT_HTTP_CONFIG, response);
}

int http_post_with_config(const char *url, const char *post_data, const char **headers,
                         const struct HTTPConfig *config, struct HTTPResponse *response)
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    long http_status = 0;
    int return_code = 0;
    struct curl_slist *curl_headers = NULL;
    struct HeaderData header_data = {0};
    APIError api_err;

    if (url == NULL || post_data == NULL || response == NULL || config == NULL) {
        fprintf(stderr, "Error: Invalid parameters\n");
        return -1;
    }

    int max_retries = config_get_int("api_max_retries", 3);
    int base_delay_ms = config_get_int("api_retry_delay_ms", 1000);
    float backoff_factor = config_get_float("api_backoff_factor", 2.0f);

    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    clear_last_api_error();

    int attempt = 0;
    while (attempt <= max_retries) {
        if (response->data != NULL) {
            free(response->data);
        }
        response->data = malloc(1);
        response->size = 0;
        header_data.retry_after_seconds = 0;

        if (response->data == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory\n");
            return -1;
        }

        curl = curl_easy_init();
        if (curl == NULL) {
            fprintf(stderr, "Error: Failed to initialize curl\n");
            free(response->data);
            response->data = NULL;
            return -1;
        }
        configure_ssl_certs(curl);

        curl_headers = curl_slist_append(NULL, "Content-Type: application/json");
        if (curl_headers == NULL) {
            fprintf(stderr, "Error: Failed to set default headers\n");
            free(response->data);
            response->data = NULL;
            curl_easy_cleanup(curl);
            return -1;
        }

        if (headers != NULL) {
            for (int i = 0; headers[i] != NULL; i++) {
                curl_headers = curl_slist_append(curl_headers, headers[i]);
                if (curl_headers == NULL) {
                    fprintf(stderr, "Error: Failed to set custom header: %s\n", headers[i]);
                    free(response->data);
                    response->data = NULL;
                    curl_easy_cleanup(curl);
                    return -1;
                }
            }
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config->connect_timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config->follow_redirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config->max_redirects);

        res = curl_easy_perform(curl);

        http_status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        }

        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
        curl_easy_cleanup(curl);
        curl = NULL;

        if (res == CURLE_OK && http_status >= 200 && http_status < 400) {
            return_code = 0;
            break;
        }

        int is_retryable = api_error_is_retryable(http_status, res);

        if (!is_retryable || attempt == max_retries) {
            api_error_set(&api_err, http_status, res, attempt + 1);
            set_last_api_error(&api_err);

            if (res != CURLE_OK) {
                debug_printf("API request failed: %s (attempt %d/%d)\n",
                            curl_easy_strerror(res), attempt + 1, max_retries + 1);
            } else {
                debug_printf("API request failed with HTTP %ld (attempt %d/%d)\n",
                            http_status, attempt + 1, max_retries + 1);
            }
            return_code = -1;
            break;
        }

        int delay_ms;
        if (header_data.retry_after_seconds > 0) {
            delay_ms = header_data.retry_after_seconds * 1000;
            debug_printf("API returned Retry-After: %d seconds\n", header_data.retry_after_seconds);
        } else {
            delay_ms = calculate_retry_delay(attempt, base_delay_ms, backoff_factor);
        }

        debug_printf("API request failed (attempt %d/%d), retrying in %dms...\n",
                    attempt + 1, max_retries + 1, delay_ms);

        usleep(delay_ms * 1000);

        attempt++;
    }

    return return_code;
}

int http_get(const char *url, struct HTTPResponse *response)
{
    return http_get_with_config(url, NULL, &DEFAULT_HTTP_CONFIG, response);
}

int http_get_with_config(const char *url, const char **headers,
                        const struct HTTPConfig *config, struct HTTPResponse *response)
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    long http_status = 0;
    int return_code = 0;
    struct curl_slist *curl_headers = NULL;
    struct HeaderData header_data = {0};
    APIError api_err;

    // Must initialize before the NULL-check below may return early
    if (response != NULL) {
        response->data = NULL;
        response->size = 0;
    }

    if (url == NULL || response == NULL || config == NULL) {
        fprintf(stderr, "Error: Invalid parameters\n");
        return -1;
    }

    int max_retries = config_get_int("api_max_retries", 3);
    int base_delay_ms = config_get_int("api_retry_delay_ms", 1000);
    float backoff_factor = config_get_float("api_backoff_factor", 2.0f);

    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    clear_last_api_error();

    int attempt = 0;
    while (attempt <= max_retries) {
        if (response->data != NULL) {
            free(response->data);
        }
        response->data = malloc(1);
        response->size = 0;
        header_data.retry_after_seconds = 0;

        if (response->data == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory\n");
            return -1;
        }

        curl = curl_easy_init();
        if (curl == NULL) {
            fprintf(stderr, "Error: Failed to initialize curl\n");
            free(response->data);
            response->data = NULL;
            return -1;
        }
        configure_ssl_certs(curl);

        curl_headers = NULL;
        if (headers != NULL) {
            for (int i = 0; headers[i] != NULL; i++) {
                curl_headers = curl_slist_append(curl_headers, headers[i]);
                if (curl_headers == NULL) {
                    fprintf(stderr, "Error: Failed to set custom header: %s\n", headers[i]);
                    free(response->data);
                    response->data = NULL;
                    curl_easy_cleanup(curl);
                    return -1;
                }
            }
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        if (curl_headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config->connect_timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config->follow_redirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config->max_redirects);

        res = curl_easy_perform(curl);

        http_status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        }

        if (curl_headers) {
            curl_slist_free_all(curl_headers);
            curl_headers = NULL;
        }
        curl_easy_cleanup(curl);
        curl = NULL;

        if (res == CURLE_OK && http_status >= 200 && http_status < 400) {
            return_code = 0;
            break;
        }

        int is_retryable = api_error_is_retryable(http_status, res);

        if (!is_retryable || attempt == max_retries) {
            api_error_set(&api_err, http_status, res, attempt + 1);
            set_last_api_error(&api_err);

            if (res != CURLE_OK) {
                debug_printf("API request failed: %s (attempt %d/%d)\n",
                            curl_easy_strerror(res), attempt + 1, max_retries + 1);
            } else {
                debug_printf("API request failed with HTTP %ld (attempt %d/%d)\n",
                            http_status, attempt + 1, max_retries + 1);
            }
            return_code = -1;
            break;
        }

        int delay_ms;
        if (header_data.retry_after_seconds > 0) {
            delay_ms = header_data.retry_after_seconds * 1000;
            debug_printf("API returned Retry-After: %d seconds\n", header_data.retry_after_seconds);
        } else {
            delay_ms = calculate_retry_delay(attempt, base_delay_ms, backoff_factor);
        }

        debug_printf("API request failed (attempt %d/%d), retrying in %dms...\n",
                    attempt + 1, max_retries + 1, delay_ms);

        usleep(delay_ms * 1000);

        attempt++;
    }

    return return_code;
}

void cleanup_response(struct HTTPResponse *response)
{
    if (response != NULL && response->data != NULL) {
        free(response->data);
        response->data = NULL;
        response->size = 0;
    }
}

const struct StreamingHTTPConfig DEFAULT_STREAMING_HTTP_CONFIG = {
    .base = {
        .timeout_seconds = 0, // SSE streams have no overall timeout; stall detection uses low_speed_*
        .connect_timeout_seconds = 30,
        .follow_redirects = 1,
        .max_redirects = 5
    },
    .stream_callback = NULL,
    .callback_data = NULL,
    .low_speed_limit = 1,
    .low_speed_time = 30
};

static size_t streaming_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    struct StreamingHTTPConfig *config = (struct StreamingHTTPConfig *)userp;
    size_t total = size * nmemb;

    if (config->stream_callback != NULL) {
        return config->stream_callback((const char *)contents, total, config->callback_data);
    }

    return total;
}

int http_post_streaming(const char *url, const char *post_data,
                       const char **headers,
                       const struct StreamingHTTPConfig *config)
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    long http_status = 0;
    struct curl_slist *curl_headers = NULL;
    APIError api_err;

    if (url == NULL || post_data == NULL || config == NULL) {
        fprintf(stderr, "Error: Invalid parameters for streaming POST\n");
        return -1;
    }

    clear_last_api_error();

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Error: Failed to initialize curl for streaming\n");
        return -1;
    }
    configure_ssl_certs(curl);

    curl_headers = curl_slist_append(NULL, "Content-Type: application/json");
    if (curl_headers == NULL) {
        fprintf(stderr, "Error: Failed to set default headers for streaming\n");
        curl_easy_cleanup(curl);
        return -1;
    }

    if (headers != NULL) {
        for (int i = 0; headers[i] != NULL; i++) {
            curl_headers = curl_slist_append(curl_headers, headers[i]);
            if (curl_headers == NULL) {
                fprintf(stderr, "Error: Failed to set custom header: %s\n", headers[i]);
                curl_easy_cleanup(curl);
                return -1;
            }
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streaming_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)config);

    if (config->base.timeout_seconds > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->base.timeout_seconds);
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config->base.connect_timeout_seconds);

    if (config->low_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, config->low_speed_limit);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, config->low_speed_time);
    }

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config->base.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config->base.max_redirects);

    res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    }

    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        api_error_set(&api_err, http_status, res, 1);
        set_last_api_error(&api_err);
        debug_printf("Streaming request failed: %s\n", curl_easy_strerror(res));
        return -1;
    }

    if (http_status < 200 || http_status >= 400) {
        api_error_set(&api_err, http_status, res, 1);
        set_last_api_error(&api_err);
        debug_printf("Streaming request failed with HTTP %ld\n", http_status);
        return -1;
    }

    return 0;
}
