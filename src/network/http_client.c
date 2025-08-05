#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Default HTTP configuration
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

int http_post(const char *url, const char *post_data, struct HTTPResponse *response)
{
    return http_post_with_config(url, post_data, NULL, &DEFAULT_HTTP_CONFIG, response);
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
    int return_code = 0;
    struct curl_slist *curl_headers = NULL;
    
    if (url == NULL || post_data == NULL || response == NULL || config == NULL) {
        fprintf(stderr, "Error: Invalid parameters\n");
        return -1;
    }
    
    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Error: Failed to initialize curl\n");
        return -1;
    }
    
    response->data = malloc(1);
    response->size = 0;
    
    if (response->data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");
    if (curl_headers == NULL) {
        fprintf(stderr, "Error: Failed to set default headers\n");
        free(response->data);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    if (headers != NULL) {
        for (int i = 0; headers[i] != NULL; i++) {
            curl_headers = curl_slist_append(curl_headers, headers[i]);
            if (curl_headers == NULL) {
                fprintf(stderr, "Error: Failed to set custom header: %s\n", headers[i]);
                free(response->data);
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config->connect_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config->follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config->max_redirects);
    
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return_code = -1;
    }
    
    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);
    return return_code;
}

int http_get(const char *url, struct HTTPResponse *response)
{
    return http_get_with_config(url, NULL, &DEFAULT_HTTP_CONFIG, response);
}

int http_get_with_headers(const char *url, const char **headers, struct HTTPResponse *response)
{
    return http_get_with_config(url, headers, &DEFAULT_HTTP_CONFIG, response);
}

int http_get_with_config(const char *url, const char **headers, 
                        const struct HTTPConfig *config, struct HTTPResponse *response)
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    int return_code = 0;
    struct curl_slist *curl_headers = NULL;
    
    if (url == NULL || response == NULL || config == NULL) {
        fprintf(stderr, "Error: Invalid parameters\n");
        return -1;
    }
    
    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Error: Failed to initialize curl\n");
        return -1;
    }
    
    response->data = malloc(1);
    response->size = 0;
    
    if (response->data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        curl_easy_cleanup(curl);
        return -1;
    }
    
    if (headers != NULL) {
        for (int i = 0; headers[i] != NULL; i++) {
            curl_headers = curl_slist_append(curl_headers, headers[i]);
            if (curl_headers == NULL) {
                fprintf(stderr, "Error: Failed to set custom header: %s\n", headers[i]);
                free(response->data);
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config->connect_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config->follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config->max_redirects);
    
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return_code = -1;
    }
    
    if (curl_headers) {
        curl_slist_free_all(curl_headers);
    }
    curl_easy_cleanup(curl);
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