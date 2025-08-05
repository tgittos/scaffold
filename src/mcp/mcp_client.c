#include "mcp_client.h"
#include <cJSON.h>
#include "../utils/debug_output.h"
#include "../network/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

// JSON-RPC message ID counter
static int g_request_id = 1;

int mcp_client_init(MCPClient* client) {
    if (!client) {
        debug_printf("MCP client is NULL\n\n");
        return -1;
    }
    
    memset(client, 0, sizeof(MCPClient));
    client->initialized = 1;
    
    debug_printf("MCP client initialized\n\n");
    return 0;
}

int mcp_find_config_path(char* config_path, size_t path_size) {
    if (!config_path || path_size == 0) {
        return -1;
    }
    
    // Try current directory first
    const char* local_config = "./ralph.config.json";
    if (access(local_config, R_OK) == 0) {
        if (strlen(local_config) >= path_size) {
            return -1;
        }
        strcpy(config_path, local_config);
        debug_printf("Found MCP config at: %s\n\n", config_path);
        return 0;
    }
    
    // Try user directory
    const char* home = getenv("HOME");
    if (home) {
        char user_config[512];
        snprintf(user_config, sizeof(user_config), "%s/.local/ralph/ralph.config.json", home);
        if (access(user_config, R_OK) == 0) {
            if (strlen(user_config) >= path_size) {
                return -1;
            }
            strcpy(config_path, user_config);
            debug_printf("Found MCP config at: %s\n\n", config_path);
            return 0;
        }
    }
    
    debug_printf("No MCP configuration file found\n\n");
    return -1;
}

char* mcp_expand_env_vars(const char* input) {
    if (!input) {
        return NULL;
    }
    
    size_t input_len = strlen(input);
    size_t output_size = input_len * 2;  // Start with 2x input size
    char* output = malloc(output_size);
    if (!output) {
        return NULL;
    }
    
    size_t output_pos = 0;
    size_t i = 0;
    
    while (i < input_len) {
        if (input[i] == '$' && i + 1 < input_len && input[i + 1] == '{') {
            // Find the end of the variable expression
            size_t var_start = i + 2;
            size_t var_end = var_start;
            int brace_count = 1;
            
            while (var_end < input_len && brace_count > 0) {
                if (input[var_end] == '{') {
                    brace_count++;
                } else if (input[var_end] == '}') {
                    brace_count--;
                }
                var_end++;
            }
            
            if (brace_count == 0) {
                // Extract variable name and default value
                size_t var_len = var_end - var_start - 1;
                char* var_expr = malloc(var_len + 1);
                if (!var_expr) {
                    free(output);
                    return NULL;
                }
                strncpy(var_expr, input + var_start, var_len);
                var_expr[var_len] = '\0';
                
                // Check for default value syntax (VAR:-default)
                char* default_sep = strstr(var_expr, ":-");
                char* var_name = var_expr;
                char* default_value = NULL;
                
                if (default_sep) {
                    *default_sep = '\0';
                    default_value = default_sep + 2;
                }
                
                // Get environment variable value
                const char* env_value = getenv(var_name);
                const char* replacement = env_value ? env_value : (default_value ? default_value : "");
                
                size_t replacement_len = strlen(replacement);
                
                // Ensure output buffer is large enough
                while (output_pos + replacement_len >= output_size) {
                    output_size *= 2;
                    char* new_output = realloc(output, output_size);
                    if (!new_output) {
                        free(output);
                        free(var_expr);
                        return NULL;
                    }
                    output = new_output;
                }
                
                // Copy replacement value
                strcpy(output + output_pos, replacement);
                output_pos += replacement_len;
                
                free(var_expr);
                i = var_end;
            } else {
                // Malformed variable expression, copy as-is
                output[output_pos++] = input[i++];
            }
        } else {
            // Regular character
            if (output_pos >= output_size - 1) {
                output_size *= 2;
                char* new_output = realloc(output, output_size);
                if (!new_output) {
                    free(output);
                    return NULL;
                }
                output = new_output;
            }
            output[output_pos++] = input[i++];
        }
    }
    
    output[output_pos] = '\0';
    return output;
}

int mcp_client_load_config(MCPClient* client, const char* config_path) {
    if (!client || !config_path) {
        debug_printf("Invalid parameters for MCP config loading\n");
        return -1;
    }
    
    // Read configuration file
    FILE* file = fopen(config_path, "r");
    if (!file) {
        debug_printf("Failed to open MCP config file: %s\n", config_path);
        return -1;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        debug_printf("MCP config file is empty or invalid size\n");
        return -1;
    }
    
    // Read file content
    char* config_json = malloc(file_size + 1);
    if (!config_json) {
        fclose(file);
        debug_printf("Failed to allocate memory for MCP config\n");
        return -1;
    }
    
    size_t read_size = fread(config_json, 1, file_size, file);
    fclose(file);
    
    if (read_size != (size_t)file_size) {
        free(config_json);
        debug_printf("Failed to read MCP config file completely\n");
        return -1;
    }
    
    config_json[file_size] = '\0';
    
    // Parse JSON
    cJSON* root = cJSON_Parse(config_json);
    free(config_json);
    
    if (!root) {
        debug_printf("Failed to parse MCP config JSON\n");
        return -1;
    }
    
    cJSON* mcp_servers = cJSON_GetObjectItem(root, "mcpServers");
    if (!mcp_servers || !cJSON_IsObject(mcp_servers)) {
        cJSON_Delete(root);
        debug_printf("No mcpServers object found in config\n");
        return -1;
    }
    
    // Count servers - for objects we need to count children
    int server_count = 0;
    cJSON* count_item = mcp_servers->child;
    while (count_item != NULL) {
        if (cJSON_IsObject(count_item)) {
            server_count++;
        }
        count_item = count_item->next;
    }
    
    if (server_count == 0) {
        cJSON_Delete(root);
        debug_printf("No MCP servers configured\n");
        return 0;  // Not an error, just no servers
    }
    
    // Allocate server configurations
    client->config.servers = malloc(server_count * sizeof(MCPServerConfig));
    if (!client->config.servers) {
        cJSON_Delete(root);
        debug_printf("Failed to allocate memory for MCP server configs\n");
        return -1;
    }
    
    client->config.server_count = 0;
    
    // Parse each server configuration
    cJSON* server_item = NULL;
    cJSON_ArrayForEach(server_item, mcp_servers) {
        if (!cJSON_IsObject(server_item)) {
            continue;
        }
        
        MCPServerConfig* config = &client->config.servers[client->config.server_count];
        memset(config, 0, sizeof(MCPServerConfig));
        
        // Server name (key)
        config->name = strdup(server_item->string);
        if (!config->name) {
            continue;
        }
        
        // Server type
        cJSON* type_item = cJSON_GetObjectItem(server_item, "type");
        if (!type_item || !cJSON_IsString(type_item)) {
            debug_printf("Missing or invalid type for server %s\n", config->name);
            free(config->name);
            continue;
        }
        
        const char* type_str = cJSON_GetStringValue(type_item);
        if (strcmp(type_str, "stdio") == 0) {
            config->type = MCP_SERVER_STDIO;
        } else if (strcmp(type_str, "sse") == 0) {
            config->type = MCP_SERVER_SSE;
        } else if (strcmp(type_str, "http") == 0) {
            config->type = MCP_SERVER_HTTP;
        } else {
            debug_printf("Unknown server type '%s' for server %s\n", type_str, config->name);
            free(config->name);
            continue;
        }
        
        // Command (for stdio servers)
        cJSON* command_item = cJSON_GetObjectItem(server_item, "command");
        if (command_item && cJSON_IsString(command_item)) {
            const char* command_str = cJSON_GetStringValue(command_item);
            config->command = mcp_expand_env_vars(command_str);
        }
        
        // URL (for SSE/HTTP servers)
        cJSON* url_item = cJSON_GetObjectItem(server_item, "url");
        if (url_item && cJSON_IsString(url_item)) {
            const char* url_str = cJSON_GetStringValue(url_item);
            config->url = mcp_expand_env_vars(url_str);
        }
        
        // Arguments (for stdio servers)
        cJSON* args_item = cJSON_GetObjectItem(server_item, "args");
        if (args_item && cJSON_IsArray(args_item)) {
            int arg_count = cJSON_GetArraySize(args_item);
            if (arg_count > 0) {
                config->args = malloc(arg_count * sizeof(char*));
                if (config->args) {
                    config->arg_count = 0;
                    cJSON* arg_item = NULL;
                    cJSON_ArrayForEach(arg_item, args_item) {
                        if (cJSON_IsString(arg_item)) {
                            const char* arg_str = cJSON_GetStringValue(arg_item);
                            config->args[config->arg_count] = mcp_expand_env_vars(arg_str);
                            if (config->args[config->arg_count]) {
                                config->arg_count++;
                            }
                        }
                    }
                }
            }
        }
        
        // Environment variables (for stdio servers)
        cJSON* env_item = cJSON_GetObjectItem(server_item, "env");
        if (env_item && cJSON_IsObject(env_item)) {
            int env_count = cJSON_GetArraySize(env_item);
            if (env_count > 0) {
                config->env_keys = malloc(env_count * sizeof(char*));
                config->env_values = malloc(env_count * sizeof(char*));
                if (config->env_keys && config->env_values) {
                    config->env_count = 0;
                    cJSON* env_entry = NULL;
                    cJSON_ArrayForEach(env_entry, env_item) {
                        if (cJSON_IsString(env_entry)) {
                            const char* env_value = cJSON_GetStringValue(env_entry);
                            config->env_keys[config->env_count] = strdup(env_entry->string);
                            config->env_values[config->env_count] = mcp_expand_env_vars(env_value);
                            if (config->env_keys[config->env_count] && config->env_values[config->env_count]) {
                                config->env_count++;
                            }
                        }
                    }
                }
            }
        }
        
        // Headers (for SSE/HTTP servers)
        cJSON* headers_item = cJSON_GetObjectItem(server_item, "headers");
        if (headers_item && cJSON_IsObject(headers_item)) {
            int header_count = cJSON_GetArraySize(headers_item);
            if (header_count > 0) {
                config->header_keys = malloc(header_count * sizeof(char*));
                config->header_values = malloc(header_count * sizeof(char*));
                if (config->header_keys && config->header_values) {
                    config->header_count = 0;
                    cJSON* header_entry = NULL;
                    cJSON_ArrayForEach(header_entry, headers_item) {
                        if (cJSON_IsString(header_entry)) {
                            const char* header_value = cJSON_GetStringValue(header_entry);
                            config->header_keys[config->header_count] = strdup(header_entry->string);
                            config->header_values[config->header_count] = mcp_expand_env_vars(header_value);
                            if (config->header_keys[config->header_count] && config->header_values[config->header_count]) {
                                config->header_count++;
                            }
                        }
                    }
                }
            }
        }
        
        config->enabled = 1;  // Enable by default
        client->config.server_count++;
        
        debug_printf("Configured MCP server: %s (type: %s)\n", config->name, type_str);
    }
    
    cJSON_Delete(root);
    
    // Store config path
    client->config.config_path = strdup(config_path);
    
    debug_printf("Loaded %d MCP servers from config\n", client->config.server_count);
    return 0;
}

int mcp_connect_server(MCPServerState* server) {
    if (!server || !server->config.enabled) {
        debug_printf("Invalid server or server disabled\n");
        return -1;
    }
    
    debug_printf("Connecting to MCP server: %s\n", server->config.name);
    
    if (server->config.type == MCP_SERVER_STDIO) {
        // Create pipes for communication
        int stdin_pipe[2], stdout_pipe[2];
        
        if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
            debug_printf("Failed to create pipes for MCP server %s\n", server->config.name);
            return -1;
        }
        
        // Fork process
        pid_t pid = fork();
        if (pid == -1) {
            debug_printf("Failed to fork process for MCP server %s\n", server->config.name);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return -1;
        }
        
        if (pid == 0) {
            // Child process - set up environment and exec
            
            // Redirect stdin/stdout
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            
            // Close unused pipe ends
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            
            // Set environment variables
            for (int i = 0; i < server->config.env_count; i++) {
                setenv(server->config.env_keys[i], server->config.env_values[i], 1);
            }
            
            // Prepare arguments
            char** argv = malloc((server->config.arg_count + 2) * sizeof(char*));
            if (!argv) {
                exit(1);
            }
            
            argv[0] = server->config.command;
            for (int i = 0; i < server->config.arg_count; i++) {
                argv[i + 1] = server->config.args[i];
            }
            argv[server->config.arg_count + 1] = NULL;
            
            // Execute server command
            execv(server->config.command, argv);
            
            // If we get here, execv failed
            debug_printf("Failed to execute MCP server command: %s\n", server->config.command);
            exit(1);
        } else {
            // Parent process - store process info
            server->process_id = pid;
            server->stdin_fd = stdin_pipe[1];
            server->stdout_fd = stdout_pipe[0];
            
            // Close unused pipe ends
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            
            // Make stdout non-blocking
            int flags = fcntl(server->stdout_fd, F_GETFL, 0);
            fcntl(server->stdout_fd, F_SETFL, flags | O_NONBLOCK);
            
            server->initialized = 1;
            
            debug_printf("Started MCP server process %d for %s\n", pid, server->config.name);
        }
    } else {
        // For HTTP/SSE servers, just mark as initialized
        server->initialized = 1;
        debug_printf("Marked HTTP/SSE server %s as initialized\n", server->config.name);
    }
    
    return 0;
}

int mcp_disconnect_server(MCPServerState* server) {
    if (!server || !server->initialized) {
        return 0;
    }
    
    debug_printf("Disconnecting MCP server: %s\n", server->config.name);
    
    if (server->config.type == MCP_SERVER_STDIO && server->process_id > 0) {
        // Close pipes
        if (server->stdin_fd > 0) {
            close(server->stdin_fd);
            server->stdin_fd = 0;
        }
        if (server->stdout_fd > 0) {
            close(server->stdout_fd);
            server->stdout_fd = 0;
        }
        
        // Terminate child process
        kill(server->process_id, SIGTERM);
        
        // Wait for process to exit
        int status;
        waitpid(server->process_id, &status, WNOHANG);
        
        server->process_id = 0;
    }
    
    server->initialized = 0;
    
    debug_printf("Disconnected MCP server: %s\n", server->config.name);
    return 0;
}

int mcp_send_request(MCPServerState* server, const char* method, const char* params, char** response) {
    if (!server || !server->initialized || !method || !response) {
        debug_printf("Invalid parameters for MCP request\n");
        return -1;
    }
    
    // Create JSON-RPC request
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddStringToObject(request, "method", method);
    cJSON_AddNumberToObject(request, "id", g_request_id++);
    
    if (params) {
        cJSON* params_json = cJSON_Parse(params);
        if (params_json) {
            cJSON_AddItemToObject(request, "params", params_json);
        } else {
            debug_printf("Failed to parse params JSON for method %s\n", method);
            cJSON_Delete(request);
            return -1;
        }
    }
    
    char* request_str = cJSON_Print(request);
    cJSON_Delete(request);
    
    if (!request_str) {
        debug_printf("Failed to serialize JSON-RPC request\n");
        return -1;
    }
    
    debug_printf("Sending MCP request to %s: %s\n", server->config.name, method);
    
    int result = -1;
    
    if (server->config.type == MCP_SERVER_STDIO) {
        // Send request via stdin
        size_t request_len = strlen(request_str);
        if (write(server->stdin_fd, request_str, request_len) == (ssize_t)request_len) {
            if (write(server->stdin_fd, "\n", 1) == 1) {
                // Read response from stdout
                char buffer[8192];
                ssize_t bytes_read = read(server->stdout_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    *response = strdup(buffer);
                    result = 0;
                } else {
                    debug_printf("Failed to read response from MCP server %s\n", server->config.name);
                }
            }
        } else {
            debug_printf("Failed to send request to MCP server %s\n", server->config.name);
        }
    } else if (server->config.type == MCP_SERVER_HTTP || server->config.type == MCP_SERVER_SSE) {
        // Send HTTP request
        char** headers = NULL;
        int header_count = server->config.header_count;
        
        if (header_count > 0) {
            headers = malloc(header_count * sizeof(char*));
            if (headers) {
                for (int i = 0; i < header_count; i++) {
                    size_t header_len = strlen(server->config.header_keys[i]) + strlen(server->config.header_values[i]) + 3;
                    headers[i] = malloc(header_len);
                    if (headers[i]) {
                        snprintf(headers[i], header_len, "%s: %s", server->config.header_keys[i], server->config.header_values[i]);
                    }
                }
            }
        }
        
        struct HTTPResponse http_response = {0};
        if (http_post_with_headers(server->config.url, request_str, (const char**)headers, &http_response) == 0) {
            if (http_response.data && http_response.size > 0) {
                *response = strdup(http_response.data);
                result = 0;
            }
        } else {
            debug_printf("Failed to send HTTP request to MCP server %s\n", server->config.name);
        }
        
        // Clean up HTTP response
        if (http_response.data) {
            free(http_response.data);
        }
        
        // Clean up headers
        if (headers) {
            for (int i = 0; i < header_count; i++) {
                free(headers[i]);
            }
            free(headers);
        }
    }
    
    free(request_str);
    
    if (result == 0) {
        debug_printf("Received MCP response from %s\n", server->config.name);
    }
    
    return result;
}

int mcp_client_connect_servers(MCPClient* client) {
    if (!client || !client->initialized) {
        debug_printf("MCP client not initialized\n");
        return -1;
    }
    
    if (client->config.server_count == 0) {
        debug_printf("No MCP servers to connect\n");
        return 0;
    }
    
    // Allocate server states
    client->servers = malloc(client->config.server_count * sizeof(MCPServerState));
    if (!client->servers) {
        debug_printf("Failed to allocate memory for MCP server states\n");
        return -1;
    }
    
    client->active_server_count = 0;
    
    // Connect to each server
    for (int i = 0; i < client->config.server_count; i++) {
        MCPServerState* server = &client->servers[client->active_server_count];
        memset(server, 0, sizeof(MCPServerState));
        
        // Copy configuration
        server->config = client->config.servers[i];
        
        // Connect to server
        if (mcp_connect_server(server) == 0) {
            client->active_server_count++;
            debug_printf("Successfully connected to MCP server: %s\n", server->config.name);
        } else {
            debug_printf("Failed to connect to MCP server: %s\n", server->config.name);
        }
    }
    
    debug_printf("Connected to %d/%d MCP servers\n", client->active_server_count, client->config.server_count);
    return 0;
}

int mcp_client_disconnect_servers(MCPClient* client) {
    if (!client || !client->servers) {
        return 0;
    }
    
    debug_printf("Disconnecting from %d MCP servers\n", client->active_server_count);
    
    for (int i = 0; i < client->active_server_count; i++) {
        mcp_disconnect_server(&client->servers[i]);
    }
    
    free(client->servers);
    client->servers = NULL;
    client->active_server_count = 0;
    
    debug_printf("Disconnected from all MCP servers\n");
    return 0;
}

int mcp_parse_tools(const char* response, ToolFunction** tools, int* tool_count) {
    if (!response || !tools || !tool_count) {
        debug_printf("Invalid parameters for MCP tool parsing\n");
        return -1;
    }
    
    *tools = NULL;
    *tool_count = 0;
    
    cJSON* json = cJSON_Parse(response);
    if (!json) {
        debug_printf("Failed to parse MCP tools response JSON\n");
        return -1;
    }
    
    cJSON* result = cJSON_GetObjectItem(json, "result");
    if (!result) {
        debug_printf("No result field in MCP tools response\n");
        cJSON_Delete(json);
        return -1;
    }
    
    cJSON* tools_array = cJSON_GetObjectItem(result, "tools");
    if (!tools_array || !cJSON_IsArray(tools_array)) {
        debug_printf("No tools array in MCP response\n");
        cJSON_Delete(json);
        return -1;
    }
    
    int count = cJSON_GetArraySize(tools_array);
    if (count == 0) {
        cJSON_Delete(json);
        return 0;  // No tools, not an error
    }
    
    ToolFunction* tool_functions = malloc(count * sizeof(ToolFunction));
    if (!tool_functions) {
        debug_printf("Failed to allocate memory for MCP tools\n");
        cJSON_Delete(json);
        return -1;
    }
    
    int parsed_count = 0;
    cJSON* tool_item = NULL;
    cJSON_ArrayForEach(tool_item, tools_array) {
        if (!cJSON_IsObject(tool_item)) {
            continue;
        }
        
        ToolFunction* func = &tool_functions[parsed_count];
        memset(func, 0, sizeof(ToolFunction));
        
        // Tool name
        cJSON* name_item = cJSON_GetObjectItem(tool_item, "name");
        if (name_item && cJSON_IsString(name_item)) {
            func->name = strdup(cJSON_GetStringValue(name_item));
        } else {
            continue;
        }
        
        // Tool description
        cJSON* desc_item = cJSON_GetObjectItem(tool_item, "description");
        if (desc_item && cJSON_IsString(desc_item)) {
            func->description = strdup(cJSON_GetStringValue(desc_item));
        }
        
        // Tool parameters
        cJSON* input_schema = cJSON_GetObjectItem(tool_item, "inputSchema");
        if (input_schema && cJSON_IsObject(input_schema)) {
            cJSON* properties = cJSON_GetObjectItem(input_schema, "properties");
            cJSON* required = cJSON_GetObjectItem(input_schema, "required");
            
            if (properties && cJSON_IsObject(properties)) {
                int param_count = cJSON_GetArraySize(properties);
                if (param_count > 0) {
                    func->parameters = malloc(param_count * sizeof(ToolParameter));
                    if (func->parameters) {
                        func->parameter_count = 0;
                        
                        cJSON* prop_item = NULL;
                        cJSON_ArrayForEach(prop_item, properties) {
                            if (!cJSON_IsObject(prop_item)) {
                                continue;
                            }
                            
                            ToolParameter* param = &func->parameters[func->parameter_count];
                            memset(param, 0, sizeof(ToolParameter));
                            
                            param->name = strdup(prop_item->string);
                            
                            cJSON* type_item = cJSON_GetObjectItem(prop_item, "type");
                            if (type_item && cJSON_IsString(type_item)) {
                                param->type = strdup(cJSON_GetStringValue(type_item));
                            }
                            
                            cJSON* desc_param = cJSON_GetObjectItem(prop_item, "description");
                            if (desc_param && cJSON_IsString(desc_param)) {
                                param->description = strdup(cJSON_GetStringValue(desc_param));
                            }
                            
                            // Check if parameter is required
                            if (required && cJSON_IsArray(required)) {
                                cJSON* req_item = NULL;
                                cJSON_ArrayForEach(req_item, required) {
                                    if (cJSON_IsString(req_item) && 
                                        strcmp(cJSON_GetStringValue(req_item), param->name) == 0) {
                                        param->required = 1;
                                        break;
                                    }
                                }
                            }
                            
                            func->parameter_count++;
                        }
                    }
                }
            }
        }
        
        parsed_count++;
    }
    
    cJSON_Delete(json);
    
    *tools = tool_functions;
    *tool_count = parsed_count;
    
    debug_printf("Parsed %d MCP tools\n", parsed_count);
    return 0;
}

int mcp_client_register_tools(MCPClient* client, ToolRegistry* registry) {
    if (!client || !registry) {
        debug_printf("Invalid parameters for MCP tool registration\n");
        return -1;
    }
    
    if (client->active_server_count == 0) {
        debug_printf("No active MCP servers to register tools from\n");
        return 0;
    }
    
    // Get tools from each server
    for (int i = 0; i < client->active_server_count; i++) {
        MCPServerState* server = &client->servers[i];
        
        char* response = NULL;
        if (mcp_send_request(server, "tools/list", NULL, &response) == 0 && response) {
            ToolFunction* tools = NULL;
            int tool_count = 0;
            
            if (mcp_parse_tools(response, &tools, &tool_count) == 0 && tool_count > 0) {
                // Store tools in server state
                server->tools = tools;
                server->tool_count = tool_count;
                
                debug_printf("Server %s provides %d tools\n", server->config.name, tool_count);
                
                // Register tools with registry
                for (int j = 0; j < tool_count; j++) {
                    ToolFunction* tool = &tools[j];
                    
                    // Create prefixed name to avoid conflicts
                    char prefixed_name[256];
                    snprintf(prefixed_name, sizeof(prefixed_name), "mcp_%s_%s", server->config.name, tool->name);
                    
                    // Find space in registry
                    int registry_idx = registry->function_count;
                    if (registry_idx >= 64) {  // Assuming max 64 tools
                        debug_printf("Tool registry full, cannot register more MCP tools\n");
                        break;
                    }
                    
                    // Copy tool to registry
                    ToolFunction* reg_tool = &registry->functions[registry_idx];
                    reg_tool->name = strdup(prefixed_name);
                    reg_tool->description = tool->description ? strdup(tool->description) : NULL;
                    reg_tool->parameters = tool->parameters;  // Share parameters
                    reg_tool->parameter_count = tool->parameter_count;
                    
                    registry->function_count++;
                    
                    debug_printf("Registered MCP tool: %s\n", prefixed_name);
                }
            }
            
            free(response);
        } else {
            debug_printf("Failed to get tools from MCP server: %s\n", server->config.name);
        }
    }
    
    return 0;
}

int mcp_client_execute_tool(MCPClient* client, const ToolCall* tool_call, ToolResult* result) {
    if (!client || !tool_call || !result) {
        debug_printf("Invalid parameters for MCP tool execution\n");
        return -1;
    }
    
    // Check if this is an MCP tool call
    if (strncmp(tool_call->name, "mcp_", 4) != 0) {
        debug_printf("Not an MCP tool call: %s\n", tool_call->name);
        return -1;
    }
    
    // Parse server name from tool name (format: mcp_servername_toolname)
    char* server_name_end = strchr(tool_call->name + 4, '_');
    if (!server_name_end) {
        debug_printf("Invalid MCP tool name format: %s\n", tool_call->name);
        return -1;
    }
    
    size_t server_name_len = server_name_end - (tool_call->name + 4);
    char server_name[256];
    if (server_name_len >= sizeof(server_name)) {
        debug_printf("MCP server name too long\n");
        return -1;
    }
    
    strncpy(server_name, tool_call->name + 4, server_name_len);
    server_name[server_name_len] = '\0';
    
    const char* tool_name = server_name_end + 1;
    
    // Find the server
    MCPServerState* server = NULL;
    for (int i = 0; i < client->active_server_count; i++) {
        if (strcmp(client->servers[i].config.name, server_name) == 0) {
            server = &client->servers[i];
            break;
        }
    }
    
    if (!server) {
        debug_printf("MCP server not found: %s\n", server_name);
        return -1;
    }
    
    debug_printf("Executing MCP tool %s on server %s\n", tool_name, server_name);
    
    // Create tool call request
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", tool_name);
    
    if (tool_call->arguments) {
        cJSON* arguments = cJSON_Parse(tool_call->arguments);
        if (arguments) {
            cJSON_AddItemToObject(params, "arguments", arguments);
        }
    }
    
    char* params_str = cJSON_Print(params);
    cJSON_Delete(params);
    
    if (!params_str) {
        debug_printf("Failed to serialize MCP tool call parameters\n");
        return -1;
    }
    
    // Send tool call request
    char* response = NULL;
    int send_result = mcp_send_request(server, "tools/call", params_str, &response);
    free(params_str);
    
    if (send_result != 0 || !response) {
        debug_printf("Failed to execute MCP tool %s on server %s\n", tool_name, server_name);
        return -1;
    }
    
    // Parse tool call response
    cJSON* response_json = cJSON_Parse(response);
    free(response);
    
    if (!response_json) {
        debug_printf("Failed to parse MCP tool call response\n");
        return -1;
    }
    
    cJSON* result_obj = cJSON_GetObjectItem(response_json, "result");
    if (!result_obj) {
        debug_printf("No result in MCP tool call response\n");
        cJSON_Delete(response_json);
        return -1;
    }
    
    // Check for error
    cJSON* error_obj = cJSON_GetObjectItem(response_json, "error");
    if (error_obj) {
        cJSON* message_obj = cJSON_GetObjectItem(error_obj, "message");
        const char* error_msg = message_obj && cJSON_IsString(message_obj) ? 
                               cJSON_GetStringValue(message_obj) : "Unknown MCP error";
        
        result->tool_call_id = strdup(tool_call->id);
        result->result = strdup(error_msg);
        result->success = 0;
        
        cJSON_Delete(response_json);
        return 0;  // Successfully processed error
    }
    
    // Get tool result content
    cJSON* content_array = cJSON_GetObjectItem(result_obj, "content");
    if (content_array && cJSON_IsArray(content_array)) {
        // Combine all content items
        cJSON* content_item = cJSON_GetArrayItem(content_array, 0);
        if (content_item) {
            cJSON* text_obj = cJSON_GetObjectItem(content_item, "text");
            if (text_obj && cJSON_IsString(text_obj)) {
                result->tool_call_id = strdup(tool_call->id);
                result->result = strdup(cJSON_GetStringValue(text_obj));
                result->success = 1;
            }
        }
    }
    
    if (!result->result) {
        // Fallback: stringify entire result
        char* result_str = cJSON_Print(result_obj);
        result->tool_call_id = strdup(tool_call->id);
        result->result = result_str ? result_str : strdup("Empty MCP result");
        result->success = 1;
    }
    
    cJSON_Delete(response_json);
    
    debug_printf("Successfully executed MCP tool %s\n", tool_call->name);
    return 0;
}

void mcp_cleanup_server_config(MCPServerConfig* config) {
    if (!config) {
        return;
    }
    
    free(config->name);
    free(config->command);
    free(config->url);
    
    for (int i = 0; i < config->arg_count; i++) {
        free(config->args[i]);
    }
    free(config->args);
    
    for (int i = 0; i < config->env_count; i++) {
        free(config->env_keys[i]);
        free(config->env_values[i]);
    }
    free(config->env_keys);
    free(config->env_values);
    
    for (int i = 0; i < config->header_count; i++) {
        free(config->header_keys[i]);
        free(config->header_values[i]);
    }
    free(config->header_keys);
    free(config->header_values);
    
    memset(config, 0, sizeof(MCPServerConfig));
}

void mcp_cleanup_server_state(MCPServerState* server) {
    if (!server) {
        return;
    }
    
    mcp_disconnect_server(server);
    
    // Clean up tools
    for (int i = 0; i < server->tool_count; i++) {
        ToolFunction* tool = &server->tools[i];
        free(tool->name);
        free(tool->description);
        
        for (int j = 0; j < tool->parameter_count; j++) {
            free(tool->parameters[j].name);
            free(tool->parameters[j].type);
            free(tool->parameters[j].description);
        }
        free(tool->parameters);
    }
    free(server->tools);
    
    free(server->session_id);
    
    memset(server, 0, sizeof(MCPServerState));
}

void mcp_client_cleanup(MCPClient* client) {
    if (!client) {
        return;
    }
    
    debug_printf("Cleaning up MCP client\n");
    
    // Disconnect servers
    mcp_client_disconnect_servers(client);
    
    // Clean up configurations
    for (int i = 0; i < client->config.server_count; i++) {
        mcp_cleanup_server_config(&client->config.servers[i]);
    }
    free(client->config.servers);
    
    free(client->config.config_path);
    
    memset(client, 0, sizeof(MCPClient));
    
    debug_printf("MCP client cleanup complete\n");
}