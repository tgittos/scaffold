#include "messaging_tool.h"
#include "../db/message_store.h"
#include "../utils/common_utils.h"
#include "../utils/json_escape.h"
#include "tool_result_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* g_agent_id = NULL;
static char* g_parent_agent_id = NULL;

void messaging_tool_set_agent_id(const char* agent_id) {
    free(g_agent_id);
    g_agent_id = agent_id ? strdup(agent_id) : NULL;
}

const char* messaging_tool_get_agent_id(void) {
    return g_agent_id;
}

void messaging_tool_set_parent_agent_id(const char* parent_id) {
    free(g_parent_agent_id);
    g_parent_agent_id = parent_id ? strdup(parent_id) : NULL;
}

const char* messaging_tool_get_parent_agent_id(void) {
    return g_parent_agent_id;
}

void messaging_tool_cleanup(void) {
    free(g_agent_id);
    g_agent_id = NULL;
    free(g_parent_agent_id);
    g_parent_agent_id = NULL;
}

int execute_get_agent_info_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    const char* agent_id = messaging_tool_get_agent_id();
    const char* parent_id = messaging_tool_get_parent_agent_id();

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

    const char* sender_id = messaging_tool_get_agent_id();
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

    message_store_t* store = message_store_get_instance();
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(recipient_id);
        free(content);
        return 0;
    }

    char msg_id[40] = {0};
    int rc = message_send_direct(store, sender_id, recipient_id, content, (int)ttl, msg_id);

    if (rc == 0) {
        tool_result_builder_set_success(builder,
            "{\"success\": true, \"message_id\": \"%s\", \"recipient\": \"%s\"}",
            msg_id, recipient_id);
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

    const char* agent_id = messaging_tool_get_agent_id();
    if (agent_id == NULL) {
        tool_result_builder_set_error(builder, "Agent ID not configured");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        return 0;
    }

    message_store_t* store = message_store_get_instance();
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        return 0;
    }

    size_t count = 0;
    DirectMessage** msgs = message_receive_direct(store, agent_id, (size_t)max_count, &count);

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
                     "{\"id\": \"%s\", \"sender\": \"%s\", \"content\": \"%s\", \"created_at\": %ld}",
                     msgs[i]->id,
                     msgs[i]->sender_id,
                     escaped_contents[i] ? escaped_contents[i] : "",
                     (long)msgs[i]->created_at);
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

    const char* agent_id = messaging_tool_get_agent_id();
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

    message_store_t* store = message_store_get_instance();
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
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
            free(channel_name);
            free(create_if_missing);
            free(description);
            return 0;
        }
    } else {
        channel_free(ch);
    }

    int rc = channel_subscribe(store, channel_name, agent_id);
    if (rc == 0) {
        tool_result_builder_set_success(builder,
            "{\"success\": true, \"channel\": \"%s\", \"subscribed\": true}",
            channel_name);
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

    const char* sender_id = messaging_tool_get_agent_id();
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

    message_store_t* store = message_store_get_instance();
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
        free(channel_name);
        free(content);
        return 0;
    }

    char msg_id[40] = {0};
    int rc = channel_publish(store, channel_name, sender_id, content, msg_id);

    if (rc == 0) {
        tool_result_builder_set_success(builder,
            "{\"success\": true, \"message_id\": \"%s\", \"channel\": \"%s\"}",
            msg_id, channel_name);
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

    const char* agent_id = messaging_tool_get_agent_id();
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

    message_store_t* store = message_store_get_instance();
    if (store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access message store");
        ToolResult* temp = tool_result_builder_finalize(builder);
        if (temp) {
            *result = *temp;
            free(temp);
        }
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
                     "{\"id\": \"%s\", \"channel\": \"%s\", \"sender\": \"%s\", \"content\": \"%s\", \"created_at\": %ld}",
                     msgs[i]->id,
                     msgs[i]->channel_id,
                     msgs[i]->sender_id,
                     escaped_contents[i] ? escaped_contents[i] : "",
                     (long)msgs[i]->created_at);
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

static void free_tool_params(ToolParameter* params, int count) {
    for (int i = 0; i < count; i++) {
        free(params[i].name);
        free(params[i].type);
        free(params[i].description);
    }
}

int register_messaging_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    int rc;

    // send_message tool
    ToolParameter send_params[3];
    memset(send_params, 0, sizeof(send_params));

    send_params[0].name = strdup("recipient_id");
    send_params[0].type = strdup("string");
    send_params[0].description = strdup("The ID of the agent to send the message to");
    send_params[0].required = 1;

    send_params[1].name = strdup("content");
    send_params[1].type = strdup("string");
    send_params[1].description = strdup("The message content to send");
    send_params[1].required = 1;

    send_params[2].name = strdup("ttl_seconds");
    send_params[2].type = strdup("number");
    send_params[2].description = strdup("Time-to-live in seconds (0 = no expiry)");
    send_params[2].required = 0;

    for (int i = 0; i < 3; i++) {
        if (send_params[i].name == NULL || send_params[i].type == NULL ||
            send_params[i].description == NULL) {
            free_tool_params(send_params, 3);
            return -1;
        }
    }

    rc = register_tool(registry, "send_message",
                       "Send a direct message to another agent by ID",
                       send_params, 3, execute_send_message_tool_call);
    free_tool_params(send_params, 3);
    if (rc != 0) return -1;

    // check_messages tool
    ToolParameter check_params[1];
    memset(check_params, 0, sizeof(check_params));

    check_params[0].name = strdup("max_count");
    check_params[0].type = strdup("number");
    check_params[0].description = strdup("Maximum number of messages to retrieve (default: 10)");
    check_params[0].required = 0;

    if (check_params[0].name == NULL || check_params[0].type == NULL ||
        check_params[0].description == NULL) {
        free_tool_params(check_params, 1);
        return -1;
    }

    rc = register_tool(registry, "check_messages",
                       "Check for pending direct messages sent to this agent",
                       check_params, 1, execute_check_messages_tool_call);
    free_tool_params(check_params, 1);
    if (rc != 0) return -1;

    // subscribe_channel tool
    ToolParameter sub_params[3];
    memset(sub_params, 0, sizeof(sub_params));

    sub_params[0].name = strdup("channel");
    sub_params[0].type = strdup("string");
    sub_params[0].description = strdup("The channel name to subscribe to");
    sub_params[0].required = 1;

    sub_params[1].name = strdup("create_if_missing");
    sub_params[1].type = strdup("string");
    sub_params[1].description = strdup("Set to 'true' to create the channel if it doesn't exist");
    sub_params[1].required = 0;

    sub_params[2].name = strdup("description");
    sub_params[2].type = strdup("string");
    sub_params[2].description = strdup("Description for the channel (only used when creating)");
    sub_params[2].required = 0;

    for (int i = 0; i < 3; i++) {
        if (sub_params[i].name == NULL || sub_params[i].type == NULL ||
            sub_params[i].description == NULL) {
            free_tool_params(sub_params, 3);
            return -1;
        }
    }

    rc = register_tool(registry, "subscribe_channel",
                       "Subscribe to a pub/sub channel to receive broadcast messages",
                       sub_params, 3, execute_subscribe_channel_tool_call);
    free_tool_params(sub_params, 3);
    if (rc != 0) return -1;

    // publish_channel tool
    ToolParameter pub_params[2];
    memset(pub_params, 0, sizeof(pub_params));

    pub_params[0].name = strdup("channel");
    pub_params[0].type = strdup("string");
    pub_params[0].description = strdup("The channel name to publish to");
    pub_params[0].required = 1;

    pub_params[1].name = strdup("content");
    pub_params[1].type = strdup("string");
    pub_params[1].description = strdup("The message content to broadcast");
    pub_params[1].required = 1;

    for (int i = 0; i < 2; i++) {
        if (pub_params[i].name == NULL || pub_params[i].type == NULL ||
            pub_params[i].description == NULL) {
            free_tool_params(pub_params, 2);
            return -1;
        }
    }

    rc = register_tool(registry, "publish_channel",
                       "Publish a message to a channel (broadcast to all subscribers)",
                       pub_params, 2, execute_publish_channel_tool_call);
    free_tool_params(pub_params, 2);
    if (rc != 0) return -1;

    // check_channel_messages tool
    ToolParameter check_ch_params[2];
    memset(check_ch_params, 0, sizeof(check_ch_params));

    check_ch_params[0].name = strdup("channel");
    check_ch_params[0].type = strdup("string");
    check_ch_params[0].description = strdup("Specific channel to check (omit for all subscribed channels)");
    check_ch_params[0].required = 0;

    check_ch_params[1].name = strdup("max_count");
    check_ch_params[1].type = strdup("number");
    check_ch_params[1].description = strdup("Maximum number of messages to retrieve (default: 10)");
    check_ch_params[1].required = 0;

    for (int i = 0; i < 2; i++) {
        if (check_ch_params[i].name == NULL || check_ch_params[i].type == NULL ||
            check_ch_params[i].description == NULL) {
            free_tool_params(check_ch_params, 2);
            return -1;
        }
    }

    rc = register_tool(registry, "check_channel_messages",
                       "Check for unread messages from subscribed channels",
                       check_ch_params, 2, execute_check_channel_messages_tool_call);
    free_tool_params(check_ch_params, 2);
    if (rc != 0) return -1;

    // get_agent_info tool (no parameters)
    rc = register_tool(registry, "get_agent_info",
                       "Get this agent's ID and parent agent ID (if running as a subagent). "
                       "Use this to discover your agent_id for sharing with other agents, "
                       "or to get your parent's ID to send messages back to them.",
                       NULL, 0, execute_get_agent_info_tool_call);
    if (rc != 0) return -1;

    return 0;
}
