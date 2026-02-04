/*
 * mcp_transport.h - MCP Transport Abstraction
 *
 * Provides a strategy pattern for MCP server communication.
 * Each transport type (STDIO, HTTP, SSE) implements this interface.
 */

#ifndef MCP_TRANSPORT_H
#define MCP_TRANSPORT_H

#include "mcp_client.h"
#include <sys/types.h>  /* for pid_t */

/*
 * Forward declaration for transport structure.
 */
typedef struct MCPTransport MCPTransport;

/*
 * Transport vtable - defines operations for each transport type.
 */
typedef struct MCPTransportOps {
    /*
     * Connect to the server. For STDIO, this forks the process.
     * For HTTP, this is a no-op (connection is per-request).
     *
     * Returns: 0 on success, -1 on error.
     */
    int (*connect)(MCPTransport *transport, const MCPServerConfig *config);

    /*
     * Disconnect from the server. For STDIO, this terminates the process.
     * For HTTP, this is a no-op.
     *
     * Returns: 0 on success, -1 on error.
     */
    int (*disconnect)(MCPTransport *transport);

    /*
     * Send a request and receive a response.
     *
     * Parameters:
     *   transport - The transport instance
     *   request   - JSON-RPC request string
     *   response  - Output parameter for response (caller must free)
     *
     * Returns: 0 on success, -1 on error.
     */
    int (*send_request)(MCPTransport *transport, const char *request, char **response);

    /*
     * Free resources associated with the transport.
     */
    void (*destroy)(MCPTransport *transport);
} MCPTransportOps;

/*
 * Transport instance - contains vtable and transport-specific state.
 */
struct MCPTransport {
    const MCPTransportOps *ops;
    const MCPServerConfig *config;  /* Borrowed, not owned */
    void *data;                     /* Transport-specific state */
    int connected;
};

/*
 * STDIO transport-specific data.
 * Shared definition to ensure consistent struct layout.
 */
typedef struct {
    pid_t process_id;
    int stdin_fd;
    int stdout_fd;
} StdioTransportData;

/*
 * HTTP transport-specific data.
 */
typedef struct {
    char **headers;       /* Cached headers array */
    size_t header_count;
} HttpTransportData;

/*
 * Create a transport for the given server type.
 *
 * Parameters:
 *   type - MCP_SERVER_STDIO, MCP_SERVER_HTTP, or MCP_SERVER_SSE
 *
 * Returns: New transport instance, or NULL on error. Caller must free with destroy().
 */
MCPTransport *mcp_transport_create(MCPServerType type);

/*
 * STDIO transport - communicates via child process stdin/stdout.
 */
extern const MCPTransportOps mcp_transport_stdio_ops;

/*
 * HTTP transport - communicates via HTTP POST requests.
 */
extern const MCPTransportOps mcp_transport_http_ops;

#endif /* MCP_TRANSPORT_H */
