#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <stddef.h>
#include "../tools/tools_system.h"
#include "../utils/ptrarray.h"
#include "../utils/darray.h"

/**
 * MCP Server Types
 */
typedef enum {
    MCP_SERVER_STDIO,  // Local process with stdin/stdout communication
    MCP_SERVER_SSE,    // Server-Sent Events HTTP endpoint
    MCP_SERVER_HTTP    // HTTP endpoint
} MCPServerType;

/**
 * Key-value pair for environment variables and HTTP headers
 */
typedef struct {
    char* key;
    char* value;
} KeyValue;

/**
 * Dynamic array declarations
 */
DARRAY_DECLARE(ToolFunctionArray, ToolFunction)
DARRAY_DECLARE(KeyValueArray, KeyValue)

/**
 * MCP Server Configuration
 */
typedef struct MCPServerConfig {
    char* name;                  // Server name (key in config)
    MCPServerType type;          // Server type
    char* command;               // Command path (for stdio)
    char* url;                   // URL (for SSE/HTTP)
    StringArray args;            // Command arguments (for stdio)
    KeyValueArray env_vars;      // Environment variables (for stdio)
    KeyValueArray headers;       // HTTP headers (for SSE/HTTP)
    int enabled;                 // Whether server is enabled
} MCPServerConfig;

DARRAY_DECLARE(MCPServerConfigArray, MCPServerConfig)

/**
 * MCP Server Runtime State
 */
typedef struct MCPServerState {
    MCPServerConfig* config;     // Pointer to server configuration (borrowed, not owned)
    int process_id;              // Process ID (for stdio servers)
    int stdin_fd;                // stdin file descriptor (for stdio)
    int stdout_fd;               // stdout file descriptor (for stdio)
    int initialized;             // Whether server is initialized
    ToolFunctionArray tools;     // Available tools from this server
} MCPServerState;

DARRAY_DECLARE(MCPServerStateArray, MCPServerState)

/**
 * MCP Client Configuration
 */
typedef struct {
    MCPServerConfigArray servers;  // Array of server configurations
    char* config_path;              // Path to configuration file
} MCPClientConfig;

/**
 * MCP Client State
 */
typedef struct {
    MCPClientConfig config;        // Client configuration
    MCPServerStateArray servers;   // Array of server runtime states
    int initialized;               // Whether client is initialized
} MCPClient;

/**
 * Initialize MCP client
 * 
 * @param client Pointer to MCP client structure
 * @return 0 on success, -1 on failure
 */
int mcp_client_init(MCPClient* client);

/**
 * Load MCP configuration from file
 * 
 * @param client Pointer to MCP client structure
 * @param config_path Path to ralph.config.json file
 * @return 0 on success, -1 on failure
 */
int mcp_client_load_config(MCPClient* client, const char* config_path);

/**
 * Connect to all configured MCP servers
 * 
 * @param client Pointer to MCP client structure
 * @return 0 on success, -1 on failure
 */
int mcp_client_connect_servers(MCPClient* client);

/**
 * Disconnect from all MCP servers
 * 
 * @param client Pointer to MCP client structure
 * @return 0 on success, -1 on failure
 */
int mcp_client_disconnect_servers(MCPClient* client);

/**
 * Register MCP tools with the tool registry
 * 
 * @param client Pointer to MCP client structure
 * @param registry Pointer to tool registry
 * @return 0 on success, -1 on failure
 */
int mcp_client_register_tools(MCPClient* client, ToolRegistry* registry);

/**
 * Execute tool call on appropriate MCP server
 * 
 * @param client Pointer to MCP client structure
 * @param tool_call Tool call to execute
 * @param result Output tool result
 * @return 0 on success, -1 on failure
 */
int mcp_client_execute_tool(MCPClient* client, const ToolCall* tool_call, ToolResult* result);

/**
 * Connect to a single MCP server
 * 
 * @param server Pointer to server state
 * @return 0 on success, -1 on failure
 */
int mcp_connect_server(MCPServerState* server);

/**
 * Disconnect from a single MCP server
 * 
 * @param server Pointer to server state
 * @return 0 on success, -1 on failure
 */
int mcp_disconnect_server(MCPServerState* server);

/**
 * Send JSON-RPC request to MCP server
 * 
 * @param server Pointer to server state
 * @param method JSON-RPC method name
 * @param params JSON parameters string
 * @param response Output response string (caller must free)
 * @return 0 on success, -1 on failure
 */
int mcp_send_request(MCPServerState* server, const char* method, const char* params, char** response);

/**
 * Parse tools from MCP server list_tools response
 * 
 * @param response JSON response from server
 * @param tools Output array of tool functions (caller must free)
 * @param tool_count Output number of tools
 * @return 0 on success, -1 on failure
 */
int mcp_parse_tools(const char* response, ToolFunction** tools, int* tool_count);

/**
 * Find configuration file path
 * Searches for ralph.config.json in:
 * 1. ./ralph.config.json (current directory)
 * 2. ~/.local/ralph/ralph.config.json (user directory)
 * 
 * @param config_path Output buffer for config path (caller must provide buffer)
 * @param path_size Size of config_path buffer
 * @return 0 on success, -1 if not found
 */
int mcp_find_config_path(char* config_path, size_t path_size);

/**
 * Expand environment variables in string
 * Supports ${VAR} and ${VAR:-default} formats
 * 
 * @param input Input string with potential variables
 * @return Dynamically allocated expanded string, caller must free
 */
char* mcp_expand_env_vars(const char* input);

/**
 * Clean up MCP client resources
 * 
 * @param client Pointer to MCP client structure
 */
void mcp_client_cleanup(MCPClient* client);

/**
 * Clean up MCP server configuration
 * 
 * @param config Pointer to server configuration
 */
void mcp_cleanup_server_config(MCPServerConfig* config);

/**
 * Clean up MCP server state
 * 
 * @param server Pointer to server state
 */
void mcp_cleanup_server_state(MCPServerState* server);

#endif // MCP_CLIENT_H