/*
 * mcp_transport_http.c - HTTP Transport Implementation
 *
 * Implements MCP communication via HTTP POST requests.
 * Also used for SSE servers (which use HTTP for requests).
 */

#define LOG_MODULE     LOG_MOD_MCP
#define LOG_MODULE_STR "mcp"
#include "../util/log.h"
#include "mcp_transport.h"
#include "network/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void free_headers(HttpTransportData *data) {
    if (!data || !data->headers) {
        return;
    }

    for (size_t i = 0; i < data->header_count; i++) {
        free(data->headers[i]);
    }
    free(data->headers);
    data->headers = NULL;
    data->header_count = 0;
}

static int build_headers(HttpTransportData *data, const MCPServerConfig *config) {
    if (!data || !config) {
        return -1;
    }

    free_headers(data);

    size_t header_count = config->headers.count;
    if (header_count == 0) {
        return 0;
    }

    data->headers = malloc(header_count * sizeof(char *));
    if (!data->headers) {
        return -1;
    }

    for (size_t i = 0; i < header_count; i++) {
        KeyValue *kv = &config->headers.data[i];
        size_t header_len = strlen(kv->key) + strlen(kv->value) + 3;
        data->headers[i] = malloc(header_len);
        if (!data->headers[i]) {
            /* Clean up partial allocation */
            for (size_t j = 0; j < i; j++) {
                free(data->headers[j]);
            }
            free(data->headers);
            data->headers = NULL;
            return -1;
        }
        snprintf(data->headers[i], header_len, "%s: %s", kv->key, kv->value);
    }

    data->header_count = header_count;
    return 0;
}

static int http_connect(MCPTransport *transport, const MCPServerConfig *config) {
    if (!transport || !config) {
        return -1;
    }

    if (config->type != MCP_SERVER_HTTP && config->type != MCP_SERVER_SSE) {
        LOG_ERROR("HTTP transport: wrong server type");
        return -1;
    }

    if (!config->url) {
        LOG_ERROR("HTTP transport: no URL specified");
        return -1;
    }

    HttpTransportData *data = transport->data;
    if (!data) {
        return -1;
    }

    /* Pre-build headers for reuse across requests */
    if (build_headers(data, config) != 0) {
        LOG_ERROR("HTTP transport: failed to build headers");
        return -1;
    }

    transport->config = config;
    transport->connected = 1;

    LOG_INFO("HTTP transport: initialized for %s", config->name);
    return 0;
}

static int http_disconnect(MCPTransport *transport) {
    if (!transport) {
        return 0;
    }

    HttpTransportData *data = transport->data;
    if (data) {
        free_headers(data);
    }

    transport->connected = 0;

    LOG_INFO("HTTP transport: disconnected");
    return 0;
}

static int http_send_request(MCPTransport *transport, const char *request, char **response) {
    if (!transport || !transport->connected || !request || !response) {
        return -1;
    }

    if (!transport->config || !transport->config->url) {
        LOG_ERROR("HTTP transport: not configured");
        return -1;
    }

    HttpTransportData *data = transport->data;
    if (!data) {
        return -1;
    }

    *response = NULL;

    struct HTTPResponse http_response = {0};
    int result = http_post_with_headers(
        transport->config->url,
        request,
        (const char **)data->headers,
        &http_response
    );

    if (result != 0) {
        LOG_ERROR("HTTP transport: POST request failed");
        if (http_response.data) {
            free(http_response.data);
        }
        return -1;
    }

    if (!http_response.data || http_response.size == 0) {
        LOG_ERROR("HTTP transport: empty response");
        if (http_response.data) {
            free(http_response.data);
        }
        return -1;
    }

    *response = strdup(http_response.data);
    free(http_response.data);

    if (!*response) {
        return -1;
    }

    return 0;
}

static void http_destroy(MCPTransport *transport) {
    if (!transport) {
        return;
    }

    http_disconnect(transport);
    free(transport->data);
    free(transport);
}

const MCPTransportOps mcp_transport_http_ops = {
    .connect = http_connect,
    .disconnect = http_disconnect,
    .send_request = http_send_request,
    .destroy = http_destroy
};
