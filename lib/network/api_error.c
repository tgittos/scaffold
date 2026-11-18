#include "api_error.h"
#include <string.h>
#include <stdio.h>

#ifdef __STDC_NO_THREADS__
static APIError g_last_api_error = {0};
#else
static _Thread_local APIError g_last_api_error = {0};
#endif

void api_error_init(APIError *err)
{
    if (!err) return;
    err->is_retryable = 0;
    err->attempts_made = 0;
    err->http_status = 0;
    err->curl_code = CURLE_OK;
    err->error_message[0] = '\0';
    err->provider_body[0] = '\0';
}

int api_error_is_retryable(long http_status, CURLcode curl_code)
{
    if (curl_code == CURLE_COULDNT_CONNECT ||
        curl_code == CURLE_OPERATION_TIMEDOUT ||
        curl_code == CURLE_GOT_NOTHING ||
        curl_code == CURLE_RECV_ERROR ||
        curl_code == CURLE_SEND_ERROR) {
        return 1;
    }

    if (http_status == 429 ||
        http_status == 500 ||
        http_status == 502 ||
        http_status == 503 ||
        http_status == 504) {
        return 1;
    }

    return 0;
}

void api_error_set(APIError *err, long http_status, CURLcode curl_code, int attempts)
{
    if (!err) return;

    err->http_status = http_status;
    err->curl_code = curl_code;
    err->attempts_made = attempts;
    err->is_retryable = api_error_is_retryable(http_status, curl_code);
    err->provider_body[0] = '\0';

    if (curl_code != CURLE_OK) {
        snprintf(err->error_message, sizeof(err->error_message),
                 "CURL error: %s", curl_easy_strerror(curl_code));
    } else if (http_status >= 400) {
        snprintf(err->error_message, sizeof(err->error_message),
                 "HTTP error: %ld", http_status);
    } else {
        err->error_message[0] = '\0';
    }
}

/* Leniently extracts "error"."message" from a JSON blob.
 * Writes into out (max out_size bytes incl. NUL). Returns out, or NULL if
 * the field was not found. */
static const char *extract_error_message(const char *json,
                                         char *out, size_t out_size)
{
    if (!json || !out || out_size == 0) return NULL;

    const char *p = strstr(json, "\"error\"");
    if (!p) return NULL;
    p = strstr(p, "\"message\"");
    if (!p) return NULL;
    p += 9;  /* skip "message" */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return NULL;
    p++;  /* skip ':' */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return NULL;
    p++;  /* skip opening quote */

    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;  /* skip backslash; copy the escaped char as-is */
        }
        out[i++] = *p++;
    }
    if (i == 0) return NULL;
    out[i] = '\0';
    return out;
}

const char* api_error_user_message(const APIError *err)
{
    if (!err) return "Unknown error occurred.";

    /* When we have a provider body, try to extract error.message and
     * include both the HTTP status and the provider's own description. */
    if (err->provider_body[0] != '\0') {
        static char provider_msg[256];  /* not thread-safe; display-path only */
        static char formatted[1280];    /* not thread-safe; display-path only */  /* "HTTP NNNNN: " + up to 1023-byte body + NUL */
        const char *extracted = extract_error_message(err->provider_body,
                                                      provider_msg,
                                                      sizeof(provider_msg));
        if (extracted) {
            snprintf(formatted, sizeof(formatted),
                     "HTTP %ld: %s", err->http_status, extracted);
            return formatted;
        }
        /* Body present but no parseable error.message: show status + raw body
         * (truncated to fit). */
        snprintf(formatted, sizeof(formatted),
                 "HTTP %ld: %s", err->http_status, err->provider_body);
        return formatted;
    }

    if (err->curl_code != CURLE_OK) {
        switch (err->curl_code) {
            case CURLE_ABORTED_BY_CALLBACK:
                return "Operation cancelled.";
            case CURLE_COULDNT_CONNECT:
                return "Network error. Could not connect to the API server.";
            case CURLE_OPERATION_TIMEDOUT:
                return "Request timed out. The API server may be slow or unreachable.";
            case CURLE_GOT_NOTHING:
                return "Network error. No response from server.";
            case CURLE_RECV_ERROR:
            case CURLE_SEND_ERROR:
                return "Network error. Check your internet connection.";
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
                return "SSL/TLS error. Secure connection could not be established.";
            case CURLE_COULDNT_RESOLVE_HOST:
                return "Network error. Could not resolve API server hostname.";
            default:
                return "Network error. Check your internet connection.";
        }
    }

    switch (err->http_status) {
        case 429:
            return "Rate limited by API. Wait a moment and try again.";
        case 401:
            return "Authentication failed. Check your API key.";
        case 403:
            return "Access forbidden. Check your API key permissions.";
        case 400:
            return "Bad request. The request was malformed.";
        case 404:
            return "API endpoint not found. Check your API URL configuration.";
        case 500:
            return "API server error. The service may be temporarily unavailable.";
        case 502:
            return "Bad gateway. The API service may be temporarily unavailable.";
        case 503:
            return "Service unavailable. The API is temporarily overloaded.";
        case 504:
            return "Gateway timeout. The API server is not responding.";
        default:
            if (err->http_status >= 500) {
                return "API server error. The service may be temporarily unavailable.";
            } else if (err->http_status >= 400) {
                return "API request failed. Check your configuration.";
            } else if (err->http_status >= 300) {
                static char redirect_msg[128];  /* not thread-safe; display-path only */
                snprintf(redirect_msg, sizeof(redirect_msg),
                         "Unexpected redirect (HTTP %ld). Check your API URL configuration.",
                         err->http_status);
                return redirect_msg;
            }
            return "Unknown error occurred.";
    }
}

void get_last_api_error(APIError *err)
{
    if (!err) return;
    memcpy(err, &g_last_api_error, sizeof(APIError));
}

void set_last_api_error(const APIError *err)
{
    if (!err) return;
    memcpy(&g_last_api_error, err, sizeof(APIError));
}

void clear_last_api_error(void)
{
    api_error_init(&g_last_api_error);
}
