#include "api_error.h"
#include <string.h>
#include <stdio.h>

// Not actually thread-local; safe only because ralph is single-threaded
static APIError g_last_api_error = {0};

void api_error_init(APIError *err)
{
    if (!err) return;
    err->is_retryable = 0;
    err->attempts_made = 0;
    err->http_status = 0;
    err->curl_code = CURLE_OK;
    err->error_message[0] = '\0';
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

const char* api_error_user_message(const APIError *err)
{
    if (!err) return "Unknown error occurred.";

    if (err->curl_code != CURLE_OK) {
        switch (err->curl_code) {
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
