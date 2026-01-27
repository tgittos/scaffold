#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <stddef.h>
#include "../tools/tools_system.h"
#include "../utils/ptrarray.h"
#include "../utils/darray.h"

typedef enum {
    MCP_SERVER_STDIO,
    MCP_SERVER_SSE,
    MCP_SERVER_HTTP
} MCPServerType;

typedef struct {
    char* key;
    char* value;
} KeyValue;

DARRAY_DECLARE(ToolFunctionArray, ToolFunction)
DARRAY_DECLARE(KeyValueArray, KeyValue)

typedef struct MCPServerConfig {
    char* name;
    MCPServerType type;
    char* command;               // stdio only
    char* url;                   // SSE/HTTP only
    StringArray args;            // stdio only
    KeyValueArray env_vars;      // stdio only
    KeyValueArray headers;       // SSE/HTTP only
    int enabled;
} MCPServerConfig;

DARRAY_DECLARE(MCPServerConfigArray, MCPServerConfig)

typedef struct MCPServerState {
    MCPServerConfig* config;     // Borrowed from MCPClientConfig; not owned
    int process_id;
    int stdin_fd;
    int stdout_fd;
    int initialized;
    ToolFunctionArray tools;
} MCPServerState;

DARRAY_DECLARE(MCPServerStateArray, MCPServerState)

typedef struct {
    MCPServerConfigArray servers;
    char* config_path;
} MCPClientConfig;

typedef struct {
    MCPClientConfig config;
    MCPServerStateArray servers;
    int initialized;
} MCPClient;

int mcp_client_init(MCPClient* client);
int mcp_client_load_config(MCPClient* client, const char* config_path);
int mcp_client_connect_servers(MCPClient* client);
int mcp_client_disconnect_servers(MCPClient* client);

/* Tools are registered with a "mcp_<server>_<tool>" naming convention to avoid collisions. */
int mcp_client_register_tools(MCPClient* client, ToolRegistry* registry);

/* Routes the call to the correct server by parsing the "mcp_<server>_<tool>" prefix. */
int mcp_client_execute_tool(MCPClient* client, const ToolCall* tool_call, ToolResult* result);

int mcp_connect_server(MCPServerState* server);
int mcp_disconnect_server(MCPServerState* server);

/* Caller must free *response. */
int mcp_send_request(MCPServerState* server, const char* method, const char* params, char** response);

/* Caller must free *tools. */
int mcp_parse_tools(const char* response, ToolFunction** tools, int* tool_count);

/*
 * Searches for ralph.config.json in order:
 * 1. Current working directory
 * 2. ~/.local/ralph/
 */
int mcp_find_config_path(char* config_path, size_t path_size);

/* Expands ${VAR} and ${VAR:-default} syntax. Caller must free the result. */
char* mcp_expand_env_vars(const char* input);

void mcp_client_cleanup(MCPClient* client);
void mcp_cleanup_server_config(MCPServerConfig* config);
void mcp_cleanup_server_state(MCPServerState* server);

#endif // MCP_CLIENT_H