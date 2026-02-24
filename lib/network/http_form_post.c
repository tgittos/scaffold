#include "http_form_post.h"
#include "embedded_cacert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

static void configure_form_ssl(CURL *curl) {
    struct curl_blob blob;
    blob.data = (void *)embedded_cacert_data;
    blob.len = embedded_cacert_size;
    blob.flags = CURL_BLOB_NOCOPY;
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
}

int http_form_post(const char *url, const FormField *fields, int count,
                   struct HTTPResponse *response) {
    if (!url || !fields || count <= 0 || !response) return -1;

    memset(response, 0, sizeof(*response));
    response->data = malloc(1);
    if (!response->data) return -1;
    response->data[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) { free(response->data); response->data = NULL; return -1; }

    configure_form_ssl(curl);

    /* Build form body: key=urlencoded_value&... */
    char *body = NULL;
    size_t body_len = 0;

    for (int i = 0; i < count; i++) {
        char *escaped = curl_easy_escape(curl, fields[i].value, 0);
        if (!escaped) {
            free(body);
            curl_easy_cleanup(curl);
            free(response->data);
            response->data = NULL;
            return -1;
        }

        size_t key_len = strlen(fields[i].key);
        size_t val_len = strlen(escaped);
        size_t segment_len = key_len + 1 + val_len + (i < count - 1 ? 1 : 0);

        char *new_body = realloc(body, body_len + segment_len + 1);
        if (!new_body) {
            curl_free(escaped);
            free(body);
            curl_easy_cleanup(curl);
            free(response->data);
            response->data = NULL;
            return -1;
        }
        body = new_body;

        int written = snprintf(body + body_len, segment_len + 1, "%s=%s%s",
                               fields[i].key, escaped, i < count - 1 ? "&" : "");
        body_len += written;
        curl_free(escaped);
    }

    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    long http_status = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        response->http_status = http_status;
        char *ct = NULL;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        response->content_type = ct ? strdup(ct) : NULL;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);

    if (res != CURLE_OK) {
        cleanup_response(response);
        return -1;
    }

    return 0;
}
