#include "messaging_tool.h"
#include "../ipc/message_store.h"
#include "../services/services.h"
#include "../util/common_utils.h"
#include "../util/json_escape.h"
#include "tool_param_dsl.h"
#include "tool_result_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

static char* g_agent_id = NULL;
static char* g_parent_agent_id = NULL;
static Services* g_services = NULL;
static pthread_mutex_t g_agent_mutex;
static pthread_once_t g_mutex_init_once = PTHREAD_ONCE_INIT;

static void init_agent_mutex(void) {
    pthread_mutex_init(&g_agent_mutex, NULL);
}

static void ensure_mutex_initialized(void) {
    pthread_once(&g_mutex_init_once, init_agent_mutex);
}

void messaging_tool_set_agent_id(const char* agent_id) {
    ensure_mutex_initialized();
    pthread_mutex_lock(&g_agent_mutex);
    free(g_agent_id);
    g_agent_id = agent_id ? strdup(agent_id) : NULL;
    pthread_mutex_unlock(&g_agent_mutex);
}

char* messaging_tool_get_agent_id(void) {
    ensure_mutex_initialized();
    pthread_mutex_lock(&g_agent_mutex);
    char* copy = g_agent_id ? strdup(g_agent_id) : NULL;
    pthread_mutex_unlock(&g_agent_mutex);
    return copy;
}

void messaging_tool_set_parent_agent_id(const char* parent_id) {
    ensure_mutex_initialized();
    pthread_mutex_lock(&g_agent_mutex);
    free(g_parent_agent_id);
    g_parent_agent_id = parent_id ? strdup(parent_id) : NULL;
    pthread_mutex_unlock(&g_agent_mutex);
}

void messaging_tool_set_services(Services* services) {
    /* Note: Services must be set during single-threaded initialization.
     * No mutex needed since this is called once at startup before tool execution. */
    g_services = services;
}

char* messaging_tool_get_parent_agent_id(void) {
    ensure_mutex_initialized();
    pthread_mutex_lock(&g_agent_mutex);
    char* copy = g_parent_agent_id ? strdup(g_parent_agent_id) : NULL;
    pthread_mutex_unlock(&g_agent_mutex);
    return copy;
}

void messaging_tool_cleanup(void) {
    ensure_mutex_initialized();
    pthread_mutex_lock(&g_agent_mutex);
    free(g_agent_id);
    g_agent_id = NULL;
    free(g_parent_agent_id);
    g_parent_agent_id = NULL;
    pthread_mutex_unlock(&g_agent_mutex);
}

int execute_get_agent_info_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char* agent_id = messaging_tool_get_agent_id();
    char* parent_id = messaging_tool_get_parent_agent_id();

    char* escaped_agent_id = json_escape_string(agent_id ? agent_id : "");
    char* escaped_parent_id = parent_id ? json_escape_string(parent_id) : NULL;

    char response[512];
    if (escaped_parent_id != NULL) {
        snprintf(response, sizeof(response),
                 "{\"agent_id\": \"%s\", \"parent_agent_id\": \"%s\", \"is_subagent\": true}",
                 escaped_agent_id ? escaped_agent_id : "",
                 escaped_parent_id);
    } else {
        snprintf(response, sizeof(response),
                 "{\"agent_id\": \"%s\", \"parent_agent_id\": null, \"is_subagent\": false}",
                 escaped_agent_id ? escaped_agent_id : "");
    }

    free(escaped_agent_id);
    free(escaped_parent_id);
    free(agent_id);
    free(parent_id);

    tool_result_builder_set_success(builder, response);
    ToolResult* temp = tool_result_builder_finalize(builder);
    if (temp) {
        *result = *temp;
        free(temp);
    }
    return 0;
}

int execute_send_message_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char* recipient_id = extract_string_param(tool_call->arguments, "recipient_id");
    char* content = extract_string_param(tool_call->arguments, "content");
    double ttl = extract_number_param(tool_call->arguments, "ttl_seconds", 0);

    if (recipient_id == NULL || content == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: recipient_id and content are required");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(recipient_id);
        free(content);
        return 0;
    }

    char* sender_id = messaging_tool_get_agent_id();
    if (sender_id == NULL) {
        tool_result_builder_set_error(builder, "Agent ID not configured");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(recipient_id);
        free(content);
        return 0;
    }

    message_store_t* store = services_get_message_store(g_services);
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(sender_id);
        free(recipient_id);
        free(content);
        return 0;
    }

    char msg_id[40] = {0};
    int rc = message_send_direct(store, sender_id, recipient_id, content, (int)ttl, msg_id);
    free(sender_id);

    if (rc == 0) {
        char* escaped_recipient = json_escape_string(recipient_id);
        char* escaped_msg_id = json_escape_string(msg_id);
        tool_result_builder_set_success(builder,
            "{\"success\": true, \"message_id\": \"%s\", \"recipient\": \"%s\"}",
            escaped_msg_id ? escaped_msg_id : msg_id,
            escaped_recipient ? escaped_recipient : recipient_id);
        free(escaped_recipient);
        free(escaped_msg_id);
    } else {
        tool_result_builder_set_error(builder, "Failed to send message");
    }

    ToolResult* temp = tool_result_builder_finalize(builder);
    if (temp) {
        *result = *temp;
        free(temp);
    }

    free(recipient_id);
    free(content);
    return 0;
}

int execute_check_messages_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    double max_count = extract_number_param(tool_call->arguments, "max_count", 10);

    char* agent_id = messaging_tool_get_agent_id();
    if (agent_id == NULL) {
        tool_result_builder_set_error(builder, "Agent ID not configured");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        return 0;
    }

    message_store_t* store = services_get_message_store(g_services);
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(agent_id);
        return 0;
    }

    size_t count = 0;
    DirectMessage** msgs = message_receive_direct(store, agent_id, (size_t)max_count, &count);
    free(agent_id);

    size_t required_size = 128;
    char** escaped_contents = NULL;
    if (count > 0) {
        escaped_contents = calloc(count, sizeof(char*));
        if (escaped_contents == NULL) {
            tool_result_builder_set_error(builder, "Memory allocation failed");
            direct_message_free_list(msgs, count);
            ToolResult* temp = tool_result_builder_finalize(builder);
            if (temp) {
                *result = *temp;
                free(temp);
            }
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            escaped_contents[i] = json_escape_string(msgs[i]->content);
            required_size += 200 + sizeof(msgs[i]->id) + sizeof(msgs[i]->sender_id) +
                             strlen(escaped_contents[i] ? escaped_contents[i] : "");
        }
    }

    char* response = malloc(required_size);
    if (response == NULL) {
        tool_result_builder_set_error(builder, "Memory allocation failed");
        for (size_t i = 0; i < count; i++) free(escaped_contents[i]);
        free(escaped_contents);
        direct_message_free_list(msgs, count);
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        return 0;
    }

    strcpy(response, "{\"success\": true, \"messages\": [");
    char* p = response + strlen(response);

    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            p += sprintf(p, ", ");
        }

        p += snprintf(p, required_size - (p - response),
                     "{\"id\": \"%s\", \"sender\": \"%s\", \"content\": \"%s\", \"created_at\": %" PRId64 "}",
                     msgs[i]->id,
                     msgs[i]->sender_id,
                     escaped_contents[i] ? escaped_contents[i] : "",
                     msgs[i]->created_at);
        free(escaped_contents[i]);
    }
    free(escaped_contents);

    sprintf(p, "], \"count\": %zu}", count);

    tool_result_builder_set_success_json(builder, response);
    free(response);
    direct_message_free_list(msgs, count);

    ToolResult* temp = tool_result_builder_finalize(builder);
    if (temp) {
        *result = *temp;
        free(temp);
    }
    return 0;
}

int execute_subscribe_channel_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char* channel_name = extract_string_param(tool_call->arguments, "channel");
    char* create_if_missing = extract_string_param(tool_call->arguments, "create_if_missing");
    char* description = extract_string_param(tool_call->arguments, "description");

    if (channel_name == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: channel");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(create_if_missing);
        free(description);
        return 0;
    }

    char* agent_id = messaging_tool_get_agent_id();
    if (agent_id == NULL) {
        tool_result_builder_set_error(builder, "Agent ID not configured");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(channel_name);
        free(create_if_missing);
        free(description);
        return 0;
    }

    message_store_t* store = services_get_message_store(g_services);
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(agent_id);
        free(channel_name);
        free(create_if_missing);
        free(description);
        return 0;
    }

    Channel* ch = channel_get(store, channel_name);
    if (ch == NULL) {
        int should_create = (create_if_missing != NULL &&
                             (strcmp(create_if_missing, "true") == 0 ||
                              strcmp(create_if_missing, "1") == 0));
        if (should_create) {
            if (channel_create(store, channel_name, description, agent_id, 0) != 0) {
                tool_result_builder_set_error(builder, "Failed to create channel: %s", channel_name);
                ToolResult* temp = tool_result_builder_finalize(builder);
                if (temp) {
                    *result = *temp;
                    free(temp);
                }
                free(agent_id);
                free(channel_name);
                free(create_if_missing);
                free(description);
                return 0;
            }
        } else {
            tool_result_builder_set_error(builder, "Channel not found: %s", channel_name);
            ToolResult* temp = tool_result_builder_finalize(builder);
            if (temp) {
                *result = *temp;
                free(temp);
            }
            free(agent_id);
            free(channel_name);
            free(create_if_missing);
            free(description);
            return 0;
        }
    } else {
        channel_free(ch);
    }

    int rc = channel_subscribe(store, channel_name, agent_id);
    free(agent_id);
    if (rc == 0) {
        char* escaped_channel = json_escape_string(channel_name);
        tool_result_builder_set_success(builder,
            "{\"success\": true, \"channel\": \"%s\", \"subscribed\": true}",
            escaped_channel ? escaped_channel : channel_name);
        free(escaped_channel);
    } else {
        tool_result_builder_set_error(builder, "Failed to subscribe to channel: %s", channel_name);
    }

    ToolResult* temp = tool_result_builder_finalize(builder);
    if (temp) {
        *result = *temp;
        free(temp);
    }

    free(channel_name);
    free(create_if_missing);
    free(description);
    return 0;
}

int execute_publish_channel_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char* channel_name = extract_string_param(tool_call->arguments, "channel");
    char* content = extract_string_param(tool_call->arguments, "content");

    if (channel_name == NULL || content == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameters: channel and content are required");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(channel_name);
        free(content);
        return 0;
    }

    char* sender_id = messaging_tool_get_agent_id();
    if (sender_id == NULL) {
        tool_result_builder_set_error(builder, "Agent ID not configured");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(channel_name);
        free(content);
        return 0;
    }

    message_store_t* store = services_get_message_store(g_services);
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(sender_id);
        free(channel_name);
        free(content);
        return 0;
    }

    char msg_id[40] = {0};
    int rc = channel_publish(store, channel_name, sender_id, content, msg_id);
    free(sender_id);

    if (rc == 0) {
        char* escaped_msg_id = json_escape_string(msg_id);
        char* escaped_channel = json_escape_string(channel_name);
        tool_result_builder_set_success(builder,
            "{\"success\": true, \"message_id\": \"%s\", \"channel\": \"%s\"}",
            escaped_msg_id ? escaped_msg_id : msg_id,
            escaped_channel ? escaped_channel : channel_name);
        free(escaped_msg_id);
        free(escaped_channel);
    } else {
        tool_result_builder_set_error(builder, "Failed to publish to channel: %s", channel_name);
    }

    ToolResult* temp = tool_result_builder_finalize(builder);
    if (temp) {
        *result = *temp;
        free(temp);
    }

    free(channel_name);
    free(content);
    return 0;
}

int execute_check_channel_messages_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char* channel_name = extract_string_param(tool_call->arguments, "channel");
    double max_count = extract_number_param(tool_call->arguments, "max_count", 10);

    char* agent_id = messaging_tool_get_agent_id();
    if (agent_id == NULL) {
        tool_result_builder_set_error(builder, "Agent ID not configured");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(channel_name);
        return 0;
    }

    message_store_t* store = services_get_message_store(g_services);
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(agent_id);
        free(channel_name);
        return 0;
    }

    size_t count = 0;
    ChannelMessage** msgs = NULL;

    if (channel_name != NULL) {
        msgs = channel_receive(store, channel_name, agent_id, (size_t)max_count, &count);
    } else {
        msgs = channel_receive_all(store, agent_id, (size_t)max_count, &count);
    }
    free(agent_id);

    size_t required_size = 128;
    char** escaped_contents = NULL;
    if (count > 0) {
        escaped_contents = calloc(count, sizeof(char*));
        if (escaped_contents == NULL) {
            tool_result_builder_set_error(builder, "Memory allocation failed");
            channel_message_free_list(msgs, count);
            ToolResult* temp = tool_result_builder_finalize(builder);
            if (temp) {
                *result = *temp;
                free(temp);
            }
            free(channel_name);
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            escaped_contents[i] = json_escape_string(msgs[i]->content);
            required_size += 200 + sizeof(msgs[i]->id) + sizeof(msgs[i]->channel_id) +
                             sizeof(msgs[i]->sender_id) +
                             strlen(escaped_contents[i] ? escaped_contents[i] : "");
        }
    }

    char* response = malloc(required_size);
    if (response == NULL) {
        tool_result_builder_set_error(builder, "Memory allocation failed");
        for (size_t i = 0; i < count; i++) free(escaped_contents[i]);
        free(escaped_contents);
        channel_message_free_list(msgs, count);
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(channel_name);
        return 0;
    }

    strcpy(response, "{\"success\": true, \"messages\": [");
    char* p = response + strlen(response);

    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            p += sprintf(p, ", ");
        }

        p += snprintf(p, required_size - (p - response),
                     "{\"id\": \"%s\", \"channel\": \"%s\", \"sender\": \"%s\", \"content\": \"%s\", \"created_at\": %" PRId64 "}",
                     msgs[i]->id,
                     msgs[i]->channel_id,
                     msgs[i]->sender_id,
                     escaped_contents[i] ? escaped_contents[i] : "",
                     msgs[i]->created_at);
        free(escaped_contents[i]);
    }
    free(escaped_contents);

    sprintf(p, "], \"count\": %zu}", count);

    tool_result_builder_set_success_json(builder, response);
    free(response);
    channel_message_free_list(msgs, count);

    ToolResult* temp = tool_result_builder_finalize(builder);
    if (temp) {
        *result = *temp;
        free(temp);
    }

    free(channel_name);
    return 0;
}

static const char *MESSAGING_OPERATIONS[] = {
    "send_message", "check_messages", "subscribe_channel",
    "publish_channel", "check_channel_messages", "get_agent_info", NULL
};

static const ParamDef MESSAGING_DISPATCH_PARAMS[] = {
    {"operation", "string", "Operation to perform: send_message, check_messages, subscribe_channel, publish_channel, check_channel_messages, get_agent_info", MESSAGING_OPERATIONS, 1},
    {"recipient_id", "string", "Agent ID to send message to (send_message)", NULL, 0},
    {"content", "string", "Message content (send_message, publish_channel)", NULL, 0},
    {"ttl_seconds", "number", "Time-to-live in seconds, 0 = no expiry (send_message)", NULL, 0},
    {"max_count", "number", "Maximum messages to retrieve (check_messages, check_channel_messages, default: 10)", NULL, 0},
    {"channel", "string", "Channel name (subscribe_channel, publish_channel, check_channel_messages)", NULL, 0},
    {"create_if_missing", "string", "Set to 'true' to create channel if it doesn't exist (subscribe_channel)", NULL, 0},
    {"description", "string", "Channel description when creating (subscribe_channel)", NULL, 0},
};

static const ToolDef MESSAGING_TOOL_DEF = {
    "messaging", "Inter-agent messaging: send/check direct messages, subscribe/publish channels, check channel messages, get agent info. Use 'operation' to select the action.",
    MESSAGING_DISPATCH_PARAMS, sizeof(MESSAGING_DISPATCH_PARAMS) / sizeof(MESSAGING_DISPATCH_PARAMS[0]), execute_messaging_dispatch
};

static const OperationDispatchEntry MESSAGING_DISPATCH[] = {
    {"send_message",           execute_send_message_tool_call},
    {"check_messages",         execute_check_messages_tool_call},
    {"subscribe_channel",      execute_subscribe_channel_tool_call},
    {"publish_channel",        execute_publish_channel_tool_call},
    {"check_channel_messages", execute_check_channel_messages_tool_call},
    {"get_agent_info",         execute_get_agent_info_tool_call},
};

int execute_messaging_dispatch(const ToolCall *tool_call, ToolResult *result) {
    return dispatch_by_operation(tool_call, result, MESSAGING_DISPATCH,
        sizeof(MESSAGING_DISPATCH) / sizeof(MESSAGING_DISPATCH[0]));
}

int register_messaging_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    return register_tool_from_def(registry, &MESSAGING_TOOL_DEF);
}
