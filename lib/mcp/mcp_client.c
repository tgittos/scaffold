#include "mcp_client.h"
#include "mcp_transport.h"
#include <cJSON.h>
#include "util/debug_output.h"
#include "../util/ralph_home.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

DARRAY_DEFINE(KeyValueArray, KeyValue)
DARRAY_DEFINE(MCPServerConfigArray, MCPServerConfig)
DARRAY_DEFINE(MCPServerStateArray, MCPServerState)

static atomic_int g_request_id = 1;

/* Caller transfers ownership of key and value; freed on failure */
static int keyvalue_array_add(KeyValueArray* arr, char* key, char* value) {
    if (!arr || !key || !value) {
        free(key);
        free(value);
        return -1;
    }

    KeyValue kv = { .key = key, .value = value };
    if (KeyValueArray_push(arr, kv) != 0) {
        free(key);
        free(value);
        return -1;
    }
    return 0;
}

static void keyvalue_array_cleanup(KeyValueArray* arr) {
    if (!arr) return;
    for (size_t i = 0; i < arr->count; i++) {
        free(arr->data[i].key);
        free(arr->data[i].value);
    }
    KeyValueArray_destroy(arr);
}

static void parse_json_to_keyvalue_array(cJSON* obj, KeyValueArray* arr) {
    if (!obj || !cJSON_IsObject(obj) || !arr) return;
    cJSON* entry = NULL;
    cJSON_ArrayForEach(entry, obj) {
        if (cJSON_IsString(entry)) {
            char* key = strdup(entry->string);
            char* value = mcp_expand_env_vars(cJSON_GetStringValue(entry));
            keyvalue_array_add(arr, key, value);
        }
    }
}

static void free_tool_function(ToolFunction* tool) {
    if (!tool) return;
    free(tool->name);
    free(tool->description);
    for (int i = 0; i < tool->parameter_count; i++) {
        free(tool->parameters[i].name);
        free(tool->parameters[i].type);
        free(tool->parameters[i].description);
    }
    free(tool->parameters);
}

int mcp_client_init(MCPClient* client) {
    if (!client) {
        debug_printf("MCP client is NULL\n\n");
        return -1;
    }

    memset(client, 0, sizeof(MCPClient));

    if (MCPServerConfigArray_init(&client->config.servers) != 0) {
        debug_printf("Failed to initialize server config array\n\n");
        return -1;
    }

    if (MCPServerStateArray_init(&client->servers) != 0) {
        debug_printf("Failed to initialize server state array\n\n");
        MCPServerConfigArray_destroy(&client->config.servers);
        return -1;
    }

    client->initialized = 1;

    debug_printf("MCP client initialized\n\n");
    return 0;
}

int mcp_find_config_path(char* config_path, size_t path_size) {
    if (!config_path || path_size == 0) {
        return -1;
    }

    const char* local_config = "./ralph.config.json";
    if (access(local_config, R_OK) == 0) {
        if (strlen(local_config) >= path_size) {
            return -1;
        }
        strcpy(config_path, local_config);
        debug_printf("Found MCP config at: %s\n\n", config_path);
        return 0;
    }

    char* user_config = ralph_home_path("ralph.config.json");
    if (user_config) {
        if (access(user_config, R_OK) == 0) {
            if (strlen(user_config) >= path_size) {
                free(user_config);
                return -1;
            }
            strcpy(config_path, user_config);
            debug_printf("Found MCP config at: %s\n\n", config_path);
            free(user_config);
            return 0;
        }
        free(user_config);
    }

    debug_printf("No MCP configuration file found\n\n");
    return -1;
}

char* mcp_expand_env_vars(const char* input) {
    if (!input) {
        return NULL;
    }

    size_t input_len = strlen(input);
    size_t output_size = input_len * 2;
    char* output = malloc(output_size);
    if (!output) {
        return NULL;
    }

    size_t output_pos = 0;
    size_t i = 0;

    while (i < input_len) {
        if (input[i] == '$' && i + 1 < input_len && input[i + 1] == '{') {
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
                size_t var_len = var_end - var_start - 1;
                char* var_expr = malloc(var_len + 1);
                if (!var_expr) {
                    free(output);
                    return NULL;
                }
                strncpy(var_expr, input + var_start, var_len);
                var_expr[var_len] = '\0';

                char* default_sep = strstr(var_expr, ":-");
                char* var_name = var_expr;
                char* default_value = NULL;

                if (default_sep) {
                    *default_sep = '\0';
                    default_value = default_sep + 2;
                }

                const char* env_value = getenv(var_name);
                const char* replacement = env_value ? env_value : (default_value ? default_value : "");

                size_t replacement_len = strlen(replacement);

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

                strcpy(output + output_pos, replacement);
                output_pos += replacement_len;

                free(var_expr);
                i = var_end;
            } else {
                output[output_pos++] = input[i++];
            }
        } else {
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

    FILE* file = fopen(config_path, "r");
    if (!file) {
        debug_printf("Failed to open MCP config file: %s\n", config_path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        debug_printf("MCP config file is empty or invalid size\n");
        return -1;
    }

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
        return 0;
    }

    cJSON* server_item = NULL;
    cJSON_ArrayForEach(server_item, mcp_servers) {
        if (!cJSON_IsObject(server_item)) {
            continue;
        }

        MCPServerConfig config;
        memset(&config, 0, sizeof(MCPServerConfig));

        if (StringArray_init(&config.args, free) != 0 ||
            KeyValueArray_init(&config.env_vars) != 0 ||
            KeyValueArray_init(&config.headers) != 0) {
            debug_printf("Failed to initialize arrays for server config\n");
            StringArray_destroy(&config.args);
            keyvalue_array_cleanup(&config.env_vars);
            keyvalue_array_cleanup(&config.headers);
            continue;
        }

        config.name = strdup(server_item->string);
        if (!config.name) {
            mcp_cleanup_server_config(&config);
            continue;
        }

        cJSON* type_item = cJSON_GetObjectItem(server_item, "type");
        if (!type_item || !cJSON_IsString(type_item)) {
            debug_printf("Missing or invalid type for server %s\n", config.name);
            mcp_cleanup_server_config(&config);
            continue;
        }

        const char* type_str = cJSON_GetStringValue(type_item);
        if (strcmp(type_str, "stdio") == 0) {
            config.type = MCP_SERVER_STDIO;
        } else if (strcmp(type_str, "sse") == 0) {
            config.type = MCP_SERVER_SSE;
        } else if (strcmp(type_str, "http") == 0) {
            config.type = MCP_SERVER_HTTP;
        } else {
            debug_printf("Unknown server type '%s' for server %s\n", type_str, config.name);
            mcp_cleanup_server_config(&config);
            continue;
        }

        cJSON* command_item = cJSON_GetObjectItem(server_item, "command");
        if (command_item && cJSON_IsString(command_item)) {
            const char* command_str = cJSON_GetStringValue(command_item);
            config.command = mcp_expand_env_vars(command_str);
        }

        cJSON* url_item = cJSON_GetObjectItem(server_item, "url");
        if (url_item && cJSON_IsString(url_item)) {
            const char* url_str = cJSON_GetStringValue(url_item);
            config.url = mcp_expand_env_vars(url_str);
        }

        cJSON* args_item = cJSON_GetObjectItem(server_item, "args");
        if (args_item && cJSON_IsArray(args_item)) {
            cJSON* arg_item = NULL;
            cJSON_ArrayForEach(arg_item, args_item) {
                if (cJSON_IsString(arg_item)) {
                    const char* arg_str = cJSON_GetStringValue(arg_item);
                    char* expanded = mcp_expand_env_vars(arg_str);
                    if (expanded) {
                        if (StringArray_push(&config.args, expanded) != 0) {
                            free(expanded);
                        }
                    }
                }
            }
        }

        parse_json_to_keyvalue_array(cJSON_GetObjectItem(server_item, "env"), &config.env_vars);
        parse_json_to_keyvalue_array(cJSON_GetObjectItem(server_item, "headers"), &config.headers);

        config.enabled = 1;

        if (MCPServerConfigArray_push(&client->config.servers, config) != 0) {
            debug_printf("Failed to add server config to array\n");
            mcp_cleanup_server_config(&config);
            continue;
        }

        debug_printf("Configured MCP server: %s (type: %s)\n", config.name, type_str);
    }

    cJSON_Delete(root);

    client->config.config_path = strdup(config_path);
    if (!client->config.config_path) {
        debug_printf("Warning: Failed to allocate memory for config path\n");
    }

    debug_printf("Loaded %zu MCP servers from config\n", client->config.servers.count);
    return 0;
}

int mcp_connect_server(MCPServerState* server) {
    if (!server || !server->config || !server->config->enabled) {
        debug_printf("Invalid server or server disabled\n");
        return -1;
    }

    debug_printf("Connecting to MCP server: %s\n", server->config->name);

    server->transport = mcp_transport_create(server->config->type);
    if (!server->transport) {
        debug_printf("Failed to create transport for MCP server %s\n", server->config->name);
        return -1;
    }

    if (server->transport->ops->connect(server->transport, server->config) != 0) {
        debug_printf("Failed to connect transport for MCP server %s\n", server->config->name);
        server->transport->ops->destroy(server->transport);
        server->transport = NULL;
        return -1;
    }

    if (pthread_mutex_init(&server->request_mutex, NULL) != 0) {
        debug_printf("Failed to init request mutex for MCP server %s\n", server->config->name);
        server->transport->ops->destroy(server->transport);
        server->transport = NULL;
        return -1;
    }

    server->initialized = 1;
    debug_printf("Connected to MCP server: %s\n", server->config->name);

    return 0;
}

int mcp_disconnect_server(MCPServerState* server) {
    if (!server || !server->initialized) {
        return 0;
    }

    debug_printf("Disconnecting MCP server: %s\n",
                 server->config ? server->config->name : "unknown");

    if (server->transport) {
        server->transport->ops->disconnect(server->transport);
    }

    /* Note: initialized stays true so cleanup can destroy the mutex.
     * Full teardown (initialized = 0, mutex destroy) is mcp_cleanup_server_state's job. */

    debug_printf("Disconnected MCP server: %s\n",
                 server->config ? server->config->name : "unknown");
    return 0;
}

int mcp_send_request(MCPServerState* server, const char* method, const char* params, char** response) {
    if (!server || !server->initialized || !method || !response) {
        debug_printf("Invalid parameters for MCP request\n");
        return -1;
    }

    if (!server->transport) {
        debug_printf("No transport for MCP server %s\n",
                     server->config ? server->config->name : "unknown");
        return -1;
    }

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddStringToObject(request, "method", method);
    cJSON_AddNumberToObject(request, "id", atomic_fetch_add(&g_request_id, 1));

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

    debug_printf("Sending MCP request to %s: %s\n", server->config->name, method);

    int result = server->transport->ops->send_request(server->transport, request_str, response);
    free(request_str);

    if (result == 0) {
        debug_printf("Received MCP response from %s\n", server->config->name);
    }

    return result;
}

int mcp_client_connect_servers(MCPClient* client) {
    if (!client || !client->initialized) {
        debug_printf("MCP client not initialized\n");
        return -1;
    }

    if (client->config.servers.count == 0) {
        debug_printf("No MCP servers to connect\n");
        return 0;
    }

    for (size_t i = 0; i < client->config.servers.count; i++) {
        MCPServerState server;
        memset(&server, 0, sizeof(MCPServerState));

        if (ToolFunctionArray_init(&server.tools) != 0) {
            debug_printf("Failed to initialize tools array\n");
            continue;
        }

        server.config = &client->config.servers.data[i];

        if (mcp_connect_server(&server) == 0) {
            if (MCPServerStateArray_push(&client->servers, server) != 0) {
                debug_printf("Failed to add server state to array\n");
                mcp_cleanup_server_state(&server);
            } else {
                debug_printf("Successfully connected to MCP server: %s\n", server.config->name);
            }
        } else {
            debug_printf("Failed to connect to MCP server: %s\n", server.config->name);
            mcp_cleanup_server_state(&server);
        }
    }

    debug_printf("Connected to %zu/%zu MCP servers\n", client->servers.count, client->config.servers.count);
    return 0;
}

int mcp_client_disconnect_servers(MCPClient* client) {
    if (!client) {
        return 0;
    }

    debug_printf("Disconnecting from %zu MCP servers\n", client->servers.count);

    for (size_t i = 0; i < client->servers.count; i++) {
        mcp_cleanup_server_state(&client->servers.data[i]);
    }

    MCPServerStateArray_destroy(&client->servers);

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
        return 0;
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

        cJSON* name_item = cJSON_GetObjectItem(tool_item, "name");
        if (!name_item || !cJSON_IsString(name_item)) {
            continue;
        }
        func->name = strdup(cJSON_GetStringValue(name_item));
        if (!func->name) {
            continue;
        }

        cJSON* desc_item = cJSON_GetObjectItem(tool_item, "description");
        if (desc_item && cJSON_IsString(desc_item)) {
            func->description = strdup(cJSON_GetStringValue(desc_item));
        }

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
                            if (!param->name) {
                                continue;
                            }

                            cJSON* type_item = cJSON_GetObjectItem(prop_item, "type");
                            if (type_item && cJSON_IsString(type_item)) {
                                param->type = strdup(cJSON_GetStringValue(type_item));
                            }

                            cJSON* desc_param = cJSON_GetObjectItem(prop_item, "description");
                            if (desc_param && cJSON_IsString(desc_param)) {
                                param->description = strdup(cJSON_GetStringValue(desc_param));
                            }

                            if (required && cJSON_IsArray(required) && param->name) {
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

static ToolParameter* deep_copy_parameters(const ToolParameter* src, int count) {
    if (!src || count <= 0) {
        return NULL;
    }

    ToolParameter* dst = malloc(count * sizeof(ToolParameter));
    if (!dst) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        memset(&dst[i], 0, sizeof(ToolParameter));
        dst[i].name = src[i].name ? strdup(src[i].name) : NULL;
        dst[i].type = src[i].type ? strdup(src[i].type) : NULL;
        dst[i].description = src[i].description ? strdup(src[i].description) : NULL;
        dst[i].required = src[i].required;
        dst[i].enum_count = 0;
        dst[i].enum_values = NULL;
        dst[i].items_schema = src[i].items_schema ? strdup(src[i].items_schema) : NULL;

        if (src[i].enum_count > 0 && src[i].enum_values) {
            dst[i].enum_values = malloc(src[i].enum_count * sizeof(char*));
            if (dst[i].enum_values) {
                dst[i].enum_count = src[i].enum_count;
                for (int j = 0; j < src[i].enum_count; j++) {
                    dst[i].enum_values[j] = src[i].enum_values[j] ? strdup(src[i].enum_values[j]) : NULL;
                }
            }
        }
    }

    return dst;
}

int mcp_client_register_tools(MCPClient* client, ToolRegistry* registry) {
    if (!client || !registry) {
        debug_printf("Invalid parameters for MCP tool registration\n");
        return -1;
    }

    if (client->servers.count == 0) {
        debug_printf("No active MCP servers to register tools from\n");
        return 0;
    }

    for (size_t i = 0; i < client->servers.count; i++) {
        MCPServerState* server = &client->servers.data[i];

        char* response = NULL;
        if (mcp_send_request(server, "tools/list", NULL, &response) == 0 && response) {
            ToolFunction* tools = NULL;
            int tool_count = 0;

            if (mcp_parse_tools(response, &tools, &tool_count) == 0 && tool_count > 0) {
                for (int j = 0; j < tool_count; j++) {
                    if (ToolFunctionArray_push(&server->tools, tools[j]) != 0) {
                        debug_printf("Failed to add tool to array\n");
                        free_tool_function(&tools[j]);
                    }
                }
                free(tools);

                debug_printf("Server %s provides %zu tools\n", server->config->name, server->tools.count);

                for (size_t j = 0; j < server->tools.count; j++) {
                    ToolFunction* tool = &server->tools.data[j];

                    char prefixed_name[256];
                    snprintf(prefixed_name, sizeof(prefixed_name), "mcp_%s_%s", server->config->name, tool->name);

                    ToolFunction reg_tool = {0};
                    reg_tool.name = strdup(prefixed_name);
                    if (!reg_tool.name) {
                        debug_printf("Failed to allocate memory for tool name: %s\n", prefixed_name);
                        continue;
                    }
                    reg_tool.description = tool->description ? strdup(tool->description) : NULL;
                    /* NULL execute_func signals MCP dispatch via mcp_client_execute_tool */
                    reg_tool.execute_func = NULL;
                    reg_tool.thread_safe = 1; /* Per-server mutex serializes I/O */
                    reg_tool.parameter_count = tool->parameter_count;
                    reg_tool.parameters = deep_copy_parameters(tool->parameters, tool->parameter_count);

                    if (ToolFunctionArray_push(&registry->functions, reg_tool) != 0) {
                        debug_printf("Failed to grow tool registry for MCP tool: %s\n", prefixed_name);
                        free_tool_function(&reg_tool);
                        break;
                    }

                    debug_printf("Registered MCP tool: %s\n", prefixed_name);
                }
            }

            free(response);
        } else {
            debug_printf("Failed to get tools from MCP server: %s\n", server->config->name);
        }
    }

    return 0;
}

int mcp_client_execute_tool(MCPClient* client, const ToolCall* tool_call, ToolResult* result) {
    if (!client || !tool_call || !result) {
        debug_printf("Invalid parameters for MCP tool execution\n");
        return -1;
    }

    if (strncmp(tool_call->name, "mcp_", 4) != 0) {
        debug_printf("Not an MCP tool call: %s\n", tool_call->name);
        return -1;
    }

    /* Format: mcp_<servername>_<toolname> */
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

    MCPServerState* server = NULL;
    for (size_t i = 0; i < client->servers.count; i++) {
        if (strcmp(client->servers.data[i].config->name, server_name) == 0) {
            server = &client->servers.data[i];
            break;
        }
    }

    if (!server) {
        debug_printf("MCP server not found: %s\n", server_name);
        return -1;
    }

    debug_printf("Executing MCP tool %s on server %s\n", tool_name, server_name);

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

    pthread_mutex_lock(&server->request_mutex);
    char* response = NULL;
    int send_result = mcp_send_request(server, "tools/call", params_str, &response);
    pthread_mutex_unlock(&server->request_mutex);
    free(params_str);

    if (send_result != 0 || !response) {
        debug_printf("Failed to execute MCP tool %s on server %s\n", tool_name, server_name);
        return -1;
    }

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

    cJSON* error_obj = cJSON_GetObjectItem(response_json, "error");
    if (error_obj) {
        cJSON* message_obj = cJSON_GetObjectItem(error_obj, "message");
        const char* error_msg = message_obj && cJSON_IsString(message_obj) ?
                               cJSON_GetStringValue(message_obj) : "Unknown MCP error";

        result->tool_call_id = strdup(tool_call->id);
        result->result = strdup(error_msg);
        result->success = 0;

        cJSON_Delete(response_json);
        return 0;
    }

    cJSON* content_array = cJSON_GetObjectItem(result_obj, "content");
    if (content_array && cJSON_IsArray(content_array)) {
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

    StringArray_destroy(&config->args);
    keyvalue_array_cleanup(&config->env_vars);
    keyvalue_array_cleanup(&config->headers);

    memset(config, 0, sizeof(MCPServerConfig));
}

void mcp_cleanup_server_state(MCPServerState* server) {
    if (!server) {
        return;
    }

    if (server->transport) {
        server->transport->ops->destroy(server->transport);
        server->transport = NULL;
    }

    if (server->initialized) {
        pthread_mutex_destroy(&server->request_mutex);
    }

    server->initialized = 0;

    for (size_t i = 0; i < server->tools.count; i++) {
        free_tool_function(&server->tools.data[i]);
    }
    ToolFunctionArray_destroy(&server->tools);

    memset(server, 0, sizeof(MCPServerState));
}

void mcp_client_cleanup(MCPClient* client) {
    if (!client) {
        return;
    }

    debug_printf("Cleaning up MCP client\n");

    mcp_client_disconnect_servers(client);

    for (size_t i = 0; i < client->config.servers.count; i++) {
        mcp_cleanup_server_config(&client->config.servers.data[i]);
    }
    MCPServerConfigArray_destroy(&client->config.servers);

    free(client->config.config_path);

    memset(client, 0, sizeof(MCPClient));

    debug_printf("MCP client cleanup complete\n");
}
