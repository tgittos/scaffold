#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <stddef.h>
#include "../tools/tools_system.h"

/**
 * MCP Server Types
 */
typedef enum {
    MCP_SERVER_STDIO,  // Local process with stdin/stdout communication
    MCP_SERVER_SSE,    // Server-Sent Events HTTP endpoint
    MCP_SERVER_HTTP    // HTTP endpoint
} MCPServerType;

/**
 * MCP Server Configuration
 */
typedef struct {
    char* name;                  // Server name (key in config)
    MCPServerType type;          // Server type
    char* command;               // Command path (for stdio)
    char* url;                   // URL (for SSE/HTTP)
    char** args;                 // Command arguments (for stdio)
    int arg_count;               // Number of arguments
    char** env_keys;             // Environment variable keys
    char** env_values;           // Environment variable values
    int env_count;               // Number of environment variables
    char** header_keys;          // HTTP header keys (for SSE/HTTP)
    char** header_values;        // HTTP header values (for SSE/HTTP)
    int header_count;            // Number of headers
    int enabled;                 // Whether server is enabled
} MCPServerConfig;

/**
 * MCP Server Runtime State
 */
typedef struct {
    MCPServerConfig* config;     // Pointer to server configuration (borrowed, not owned)
    int process_id;              // Process ID (for stdio servers)
    int stdin_fd;                // stdin file descriptor (for stdio)
    int stdout_fd;               // stdout file descriptor (for stdio)
    int initialized;             // Whether server is initialized
    char* session_id;            // Session identifier
    ToolFunction* tools;         // Available tools from this server
    int tool_count;              // Number of tools
} MCPServerState;

/**
 * MCP Client Configuration
 */
typedef struct {
    MCPServerConfig* servers;    // Array of server configurations
    int server_count;            // Number of configured servers
    char* config_path;           // Path to configuration file
} MCPClientConfig;

/**
 * MCP Client State
 */
typedef struct {
    MCPClientConfig config;      // Client configuration
    MCPServerState* servers;     // Array of server runtime states
    int active_server_count;     // Number of active servers
    int initialized;             // Whether client is initialized
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