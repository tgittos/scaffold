/*
 * mcp_transport.c - MCP Transport Factory
 *
 * Creates transports for the appropriate server type.
 */

#include "mcp_transport.h"
#include <stdlib.h>

/*
 * Factory function to create a transport for the given server type.
 */
MCPTransport *mcp_transport_create(MCPServerType type) {
    MCPTransport *transport = calloc(1, sizeof(MCPTransport));
    if (!transport) {
        return NULL;
    }

    switch (type) {
        case MCP_SERVER_STDIO: {
            transport->data = calloc(1, sizeof(StdioTransportData));
            if (!transport->data) {
                free(transport);
                return NULL;
            }
            transport->ops = &mcp_transport_stdio_ops;
            break;
        }

        case MCP_SERVER_HTTP:
        case MCP_SERVER_SSE: {
            transport->data = calloc(1, sizeof(HttpTransportData));
            if (!transport->data) {
                free(transport);
                return NULL;
            }
            transport->ops = &mcp_transport_http_ops;
            break;
        }

        default:
            free(transport);
            return NULL;
    }

    return transport;
}
