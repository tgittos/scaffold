#include "codex_provider.h"
#include "../llm_provider.h"
#include "../../network/api_common.h"
#include "../../network/streaming.h"
#include "../../session/conversation_tracker.h"
#include "../../tools/tools_system.h"
#include "../../ui/output_formatter.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local char tl_account_id[CODEX_MAX_ACCOUNT_ID_LEN] = {0};

void codex_set_account_id(const char *account_id) {
    if (account_id) {
        snprintf(tl_account_id, sizeof(tl_account_id), "%s", account_id);
    } else {
        tl_account_id[0] = '\0';
    }
}

const char *codex_get_account_id(void) {
    return tl_account_id[0] ? tl_account_id : NULL;
}

static int codex_detect_provider(const char *api_url) {
    if (!api_url) return 0;
    return strstr(api_url, CODEX_URL_PATTERN) != NULL;
}

/* Build Responses API request JSON */
static char *codex_build_request_json(const LLMProvider *provider,
                                       const char *model,
                                       const SystemPromptParts *system_prompt,
                                       const ConversationHistory *conversation,
                                       const char *user_message,
                                       int max_tokens,
                                       const ToolRegistry *tools) {
    (void)provider;
    if (!model || !conversation) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", model);

    /* System prompt goes in "instructions" field */
    if (system_prompt) {
        if (system_prompt->base_prompt && system_prompt->dynamic_context) {
            size_t total = strlen(system_prompt->base_prompt) + strlen(system_prompt->dynamic_context) + 4;
            char *combined = malloc(total);
            if (combined) {
                snprintf(combined, total, "%s\n\n%s", system_prompt->base_prompt, system_prompt->dynamic_context);
                cJSON_AddStringToObject(root, "instructions", combined);
                free(combined);
            } else {
                cJSON_AddStringToObject(root, "instructions", system_prompt->base_prompt);
            }
        } else if (system_prompt->base_prompt) {
            cJSON_AddStringToObject(root, "instructions", system_prompt->base_prompt);
        }
    }

    /* Build input array from conversation history */
    cJSON *input = cJSON_CreateArray();

    /* Add conversation history messages */
    if (conversation->data) {
        for (size_t i = 0; i < conversation->count; i++) {
            ConversationMessage *msg = &conversation->data[i];
            if (!msg->role || !msg->content) continue;
            /* Skip system messages (handled by instructions) */
            if (strcmp(msg->role, "system") == 0) continue;

            /* Tool results use function_call_output format */
            if (strcmp(msg->role, "tool") == 0 && msg->tool_call_id) {
                cJSON *item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "type", "function_call_output");
                cJSON_AddStringToObject(item, "call_id", msg->tool_call_id);
                cJSON_AddStringToObject(item, "output", msg->content);
                cJSON_AddItemToArray(input, item);
                continue;
            }

            /* Assistant messages with tool_calls: emit structured function_call
             * items so the Responses API can correlate with function_call_output. */
            if (strcmp(msg->role, "assistant") == 0 && msg->has_tool_calls) {
                cJSON *parsed = cJSON_Parse(msg->content);
                if (parsed) {
                    /* Emit assistant text content if present */
                    cJSON *content_val = cJSON_GetObjectItem(parsed, "content");
                    if (content_val && cJSON_IsString(content_val) &&
                        content_val->valuestring[0] != '\0') {
                        cJSON *text_item = cJSON_CreateObject();
                        cJSON_AddStringToObject(text_item, "role", "assistant");
                        cJSON_AddStringToObject(text_item, "content", content_val->valuestring);
                        cJSON_AddItemToArray(input, text_item);
                    }
                    /* Emit each tool call as a separate function_call item */
                    cJSON *tool_calls = cJSON_GetObjectItem(parsed, "tool_calls");
                    if (tool_calls && cJSON_IsArray(tool_calls)) {
                        int tc_count = cJSON_GetArraySize(tool_calls);
                        for (int t = 0; t < tc_count; t++) {
                            cJSON *tc = cJSON_GetArrayItem(tool_calls, t);
                            cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
                            cJSON *fn = cJSON_GetObjectItem(tc, "function");
                            if (!fn || !tc_id || !cJSON_IsString(tc_id)) continue;
                            cJSON *fn_name = cJSON_GetObjectItem(fn, "name");
                            cJSON *fn_args = cJSON_GetObjectItem(fn, "arguments");

                            cJSON *fc_item = cJSON_CreateObject();
                            cJSON_AddStringToObject(fc_item, "type", "function_call");
                            cJSON_AddStringToObject(fc_item, "call_id", tc_id->valuestring);
                            if (fn_name && cJSON_IsString(fn_name))
                                cJSON_AddStringToObject(fc_item, "name", fn_name->valuestring);
                            if (fn_args && cJSON_IsString(fn_args))
                                cJSON_AddStringToObject(fc_item, "arguments", fn_args->valuestring);
                            cJSON_AddItemToArray(input, fc_item);
                        }
                    }
                    cJSON_Delete(parsed);
                } else {
                    /* Parse failed: fall back to plain assistant message */
                    cJSON *item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "role", "assistant");
                    cJSON_AddStringToObject(item, "content", msg->content);
                    cJSON_AddItemToArray(input, item);
                }
                continue;
            }

            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "role", msg->role);
            cJSON_AddStringToObject(item, "content", msg->content);
            cJSON_AddItemToArray(input, item);
        }
    }

    /* Add current user message */
    if (user_message) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", "user");
        cJSON_AddStringToObject(msg, "content", user_message);
        cJSON_AddItemToArray(input, msg);
    }

    cJSON_AddItemToObject(root, "input", input);

    /* Codex subscription API does not support max_output_tokens */
    (void)max_tokens;

    /* Add tools in Responses API flat format:
       {type, name, description, parameters} not {type, function: {name, ...}} */
    if (tools && tools->functions.count > 0) {
        char *flat_json = generate_tools_json_flat(tools);
        if (flat_json) {
            cJSON *flat_arr = cJSON_Parse(flat_json);
            free(flat_json);
            if (flat_arr)
                cJSON_AddItemToObject(root, "tools", flat_arr);
        }
    }

    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

static int codex_build_headers(const LLMProvider *provider, const char *api_key,
                                const char **headers, int max_headers) {
    (void)provider;
    int count = 0;
    static _Thread_local char auth_header[MAX_AUTH_HEADER_SIZE];
    static _Thread_local char account_header[256];

    /* Content-Type is handled by http_client automatically */

    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[count++] = auth_header;
    }

    /* Add chatgpt-account-id header if set */
    const char *acct = codex_get_account_id();
    if (acct && count < max_headers - 1) {
        snprintf(account_header, sizeof(account_header), "chatgpt-account-id: %s", acct);
        headers[count++] = account_header;
    }

    return count;
}

/* Parse Responses API response format.
 * Required by the LLMProvider vtable but never called at runtime because
 * the Codex provider forces streaming mode (see session_configurator.c). */
static int codex_parse_response(const LLMProvider *provider,
                                 const char *json_response,
                                 ParsedResponse *result) {
    (void)provider;
    if (!json_response || !result) return -1;

    fprintf(stderr, "Warning: codex_parse_response called in non-streaming mode; "
            "tool calls will be ignored\n");

    cJSON *root = cJSON_Parse(json_response);
    if (!root) return -1;

    /* Check for error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            result->response_content = strdup(msg->valuestring);
        }
        cJSON_Delete(root);
        return -1;
    }

    /* Parse output array */
    cJSON *output = cJSON_GetObjectItem(root, "output");
    if (output && cJSON_IsArray(output)) {
        int arr_size = cJSON_GetArraySize(output);
        for (int i = 0; i < arr_size; i++) {
            cJSON *item = cJSON_GetArrayItem(output, i);
            cJSON *type = cJSON_GetObjectItem(item, "type");
            if (!type || !cJSON_IsString(type)) continue;

            if (strcmp(type->valuestring, "message") == 0) {
                cJSON *content = cJSON_GetObjectItem(item, "content");
                if (content && cJSON_IsArray(content)) {
                    int content_size = cJSON_GetArraySize(content);
                    for (int j = 0; j < content_size; j++) {
                        cJSON *block = cJSON_GetArrayItem(content, j);
                        cJSON *block_type = cJSON_GetObjectItem(block, "type");
                        if (block_type && cJSON_IsString(block_type) &&
                            strcmp(block_type->valuestring, "output_text") == 0) {
                            cJSON *text = cJSON_GetObjectItem(block, "text");
                            if (text && cJSON_IsString(text)) {
                                free(result->response_content);
                                result->response_content = strdup(text->valuestring);
                            }
                        }
                    }
                }
            }
        }
    }

    /* Parse usage */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *in_tok = cJSON_GetObjectItem(usage, "input_tokens");
        cJSON *out_tok = cJSON_GetObjectItem(usage, "output_tokens");
        if (in_tok && cJSON_IsNumber(in_tok))
            result->prompt_tokens = in_tok->valueint;
        if (out_tok && cJSON_IsNumber(out_tok))
            result->completion_tokens = out_tok->valueint;
    }

    cJSON_Delete(root);
    return result->response_content ? 0 : -1;
}

static int codex_supports_streaming(const LLMProvider *provider) {
    (void)provider;
    return 1;
}

/* Parse Codex streaming events:
 * - response.output_text.delta -> text content
 * - response.function_call_arguments.delta -> tool args
 * - response.completed -> done
 */
static int codex_parse_stream_event(const LLMProvider *provider,
                                     StreamingContext *ctx,
                                     const char *json_data, size_t len) {
    (void)provider;
    if (!ctx || !json_data || len == 0) return -1;

    if (len == 6 && memcmp(json_data, "[DONE]", 6) == 0) return 0;

    cJSON *root = cJSON_ParseWithLength(json_data, len);
    if (!root) return -1;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return -1;
    }

    const char *event_type = type->valuestring;

    if (strcmp(event_type, "response.output_text.delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        if (delta && cJSON_IsString(delta)) {
            streaming_emit_text(ctx, delta->valuestring, strlen(delta->valuestring));
        }
    } else if (strcmp(event_type, "response.function_call_arguments.delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        cJSON *call_id = cJSON_GetObjectItem(root, "call_id");
        cJSON *name = cJSON_GetObjectItem(root, "name");

        /* If we have a name, this is the start of a new tool call */
        if (name && cJSON_IsString(name) && call_id && cJSON_IsString(call_id)) {
            streaming_emit_tool_start(ctx, call_id->valuestring, name->valuestring);
        }

        if (delta && cJSON_IsString(delta) && call_id && cJSON_IsString(call_id)) {
            streaming_emit_tool_delta(ctx, call_id->valuestring,
                                       delta->valuestring, strlen(delta->valuestring));
        }
    } else if (strcmp(event_type, "response.completed") == 0) {
        /* Extract usage from the completed event */
        cJSON *response = cJSON_GetObjectItem(root, "response");
        if (response) {
            cJSON *usage = cJSON_GetObjectItem(response, "usage");
            if (usage) {
                cJSON *in_tok = cJSON_GetObjectItem(usage, "input_tokens");
                cJSON *out_tok = cJSON_GetObjectItem(usage, "output_tokens");
                if (in_tok && cJSON_IsNumber(in_tok)) ctx->input_tokens = in_tok->valueint;
                if (out_tok && cJSON_IsNumber(out_tok)) ctx->output_tokens = out_tok->valueint;
            }
        }
        streaming_emit_complete(ctx, "stop");
    } else if (strcmp(event_type, "response.failed") == 0 ||
               strcmp(event_type, "response.incomplete") == 0) {
        cJSON *response = cJSON_GetObjectItem(root, "response");
        if (response) {
            cJSON *status_detail = cJSON_GetObjectItem(response, "status_details");
            if (!status_detail)
                status_detail = cJSON_GetObjectItem(response, "incomplete_details");
            if (status_detail) {
                cJSON *reason = cJSON_GetObjectItem(status_detail, "reason");
                if (reason && cJSON_IsString(reason)) {
                    streaming_emit_error(ctx, reason->valuestring);
                } else {
                    streaming_emit_error(ctx, event_type);
                }
            } else {
                streaming_emit_error(ctx, event_type);
            }
        } else {
            streaming_emit_error(ctx, event_type);
        }
    } else if (strcmp(event_type, "error") == 0) {
        cJSON *error = cJSON_GetObjectItem(root, "error");
        if (error) {
            cJSON *msg = cJSON_GetObjectItem(error, "message");
            if (msg && cJSON_IsString(msg)) {
                streaming_emit_error(ctx, msg->valuestring);
            } else {
                streaming_emit_error(ctx, "unknown error");
            }
        } else {
            streaming_emit_error(ctx, "unknown error");
        }
    }

    cJSON_Delete(root);
    return 0;
}

static char *codex_build_streaming_request_json(const LLMProvider *provider,
                                                  const char *model,
                                                  const SystemPromptParts *system_prompt,
                                                  const ConversationHistory *conversation,
                                                  const char *user_message,
                                                  int max_tokens,
                                                  const ToolRegistry *tools) {
    char *base = codex_build_request_json(provider, model, system_prompt,
                                           conversation, user_message, max_tokens, tools);
    if (!base) return NULL;

    cJSON *root = cJSON_Parse(base);
    free(base);
    if (!root) return NULL;

    streaming_add_params(root, STREAM_NO_STORE);

    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

static void codex_cleanup_stream_state(const LLMProvider *provider) {
    (void)provider;
    /* Account ID is session-level state, not per-stream.
     * Clearing it here would break retries after stream errors. */
}

static LLMProvider codex_provider = {
    .capabilities = {
        .name = "Codex",
        .max_tokens_param = NULL,
        .supports_system_message = 1,
    },
    .detect_provider = codex_detect_provider,
    .build_request_json = codex_build_request_json,
    .build_headers = codex_build_headers,
    .parse_response = codex_parse_response,
    .supports_streaming = codex_supports_streaming,
    .parse_stream_event = codex_parse_stream_event,
    .build_streaming_request_json = codex_build_streaming_request_json,
    .cleanup_stream_state = codex_cleanup_stream_state,
};

int register_codex_provider(ProviderRegistry *registry) {
    return register_provider(registry, &codex_provider);
}
