#include "json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 256
#define GROWTH_FACTOR 2

// JsonBuilder implementation

static int json_builder_ensure_capacity(JsonBuilder *builder, size_t needed) {
    if (builder->error) return -1;
    
    if (builder->size + needed >= builder->capacity) {
        size_t new_capacity = builder->capacity * GROWTH_FACTOR;
        while (new_capacity < builder->size + needed + 1) {
            new_capacity *= GROWTH_FACTOR;
        }
        
        char *new_data = realloc(builder->data, new_capacity);
        if (new_data == NULL) {
            builder->error = 1;
            return -1;
        }
        
        builder->data = new_data;
        builder->capacity = new_capacity;
    }
    
    return 0;
}

static int json_builder_append(JsonBuilder *builder, const char *str) {
    if (builder->error || str == NULL) return -1;
    
    size_t len = strlen(str);
    if (json_builder_ensure_capacity(builder, len) != 0) {
        return -1;
    }
    
    strcpy(builder->data + builder->size, str);
    builder->size += len;
    return 0;
}

int json_builder_init(JsonBuilder *builder) {
    if (builder == NULL) return -1;
    
    builder->data = malloc(INITIAL_CAPACITY);
    if (builder->data == NULL) return -1;
    
    builder->data[0] = '\0';
    builder->size = 0;
    builder->capacity = INITIAL_CAPACITY;
    builder->error = 0;
    
    return 0;
}

int json_builder_start_object(JsonBuilder *builder) {
    return json_builder_append(builder, "{");
}

int json_builder_end_object(JsonBuilder *builder) {
    return json_builder_append(builder, "}");
}

int json_builder_start_array(JsonBuilder *builder) {
    return json_builder_append(builder, "[");
}

int json_builder_end_array(JsonBuilder *builder) {
    return json_builder_append(builder, "]");
}

int json_builder_add_separator(JsonBuilder *builder) {
    return json_builder_append(builder, ", ");
}

int json_builder_add_string(JsonBuilder *builder, const char *key, const char *value) {
    if (builder == NULL || key == NULL) return -1;
    
    char *escaped_value = json_escape_string(value);
    if (escaped_value == NULL) return -1;
    
    // Calculate needed size for "key": "escaped_value"
    size_t needed = strlen(key) + strlen(escaped_value) + 6; // quotes, colon, space
    if (json_builder_ensure_capacity(builder, needed) != 0) {
        free(escaped_value);
        return -1;
    }
    
    int written = snprintf(builder->data + builder->size, 
                          builder->capacity - builder->size,
                          "\"%s\": \"%s\"", key, escaped_value);
    free(escaped_value);
    
    if (written < 0) {
        builder->error = 1;
        return -1;
    }
    
    builder->size += written;
    return 0;
}

int json_builder_add_string_no_key(JsonBuilder *builder, const char *value) {
    if (builder == NULL) return -1;
    
    char *escaped_value = json_escape_string(value);
    if (escaped_value == NULL) return -1;
    
    size_t needed = strlen(escaped_value) + 2; // quotes
    if (json_builder_ensure_capacity(builder, needed) != 0) {
        free(escaped_value);
        return -1;
    }
    
    int written = snprintf(builder->data + builder->size,
                          builder->capacity - builder->size,
                          "\"%s\"", escaped_value);
    free(escaped_value);
    
    if (written < 0) {
        builder->error = 1;
        return -1;
    }
    
    builder->size += written;
    return 0;
}

int json_builder_add_object(JsonBuilder *builder, const char *key, const char *json) {
    if (builder == NULL || key == NULL || json == NULL) return -1;
    
    size_t needed = strlen(key) + strlen(json) + 5; // quotes, colon, space
    if (json_builder_ensure_capacity(builder, needed) != 0) {
        return -1;
    }
    
    int written = snprintf(builder->data + builder->size,
                          builder->capacity - builder->size,
                          "\"%s\": %s", key, json);
    
    if (written < 0) {
        builder->error = 1;
        return -1;
    }
    
    builder->size += written;
    return 0;
}

int json_builder_add_object_no_key(JsonBuilder *builder, const char *json) {
    if (builder == NULL || json == NULL) return -1;
    
    return json_builder_append(builder, json);
}

int json_builder_add_integer(JsonBuilder *builder, const char *key, int value) {
    if (builder == NULL || key == NULL) return -1;
    
    char int_str[32];
    snprintf(int_str, sizeof(int_str), "%d", value);
    
    size_t needed = strlen(key) + strlen(int_str) + 5; // quotes, colon, space
    if (json_builder_ensure_capacity(builder, needed) != 0) {
        return -1;
    }
    
    int written = snprintf(builder->data + builder->size,
                          builder->capacity - builder->size,
                          "\"%s\": %s", key, int_str);
    
    if (written < 0) {
        builder->error = 1;
        return -1;
    }
    
    builder->size += written;
    return 0;
}

int json_builder_add_boolean(JsonBuilder *builder, const char *key, int value) {
    if (builder == NULL || key == NULL) return -1;
    
    const char *bool_str = value ? "true" : "false";
    
    size_t needed = strlen(key) + strlen(bool_str) + 5; // quotes, colon, space
    if (json_builder_ensure_capacity(builder, needed) != 0) {
        return -1;
    }
    
    int written = snprintf(builder->data + builder->size,
                          builder->capacity - builder->size,
                          "\"%s\": %s", key, bool_str);
    
    if (written < 0) {
        builder->error = 1;
        return -1;
    }
    
    builder->size += written;
    return 0;
}

char* json_builder_finalize(JsonBuilder *builder) {
    if (builder == NULL || builder->error) return NULL;
    
    char *result = strdup(builder->data);
    return result;
}

void json_builder_cleanup(JsonBuilder *builder) {
    if (builder == NULL) return;
    
    free(builder->data);
    builder->data = NULL;
    builder->size = 0;
    builder->capacity = 0;
    builder->error = 0;
}

// JsonParser implementation

int json_parser_init(JsonParser *parser, const char *json) {
    if (parser == NULL || json == NULL) return -1;
    
    parser->json = json;
    parser->length = strlen(json);
    parser->position = 0;
    
    return 0;
}

static const char* find_key_position(const char *json, const char *key) {
    char search_pattern[256] = {0};
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    return strstr(json, search_pattern);
}

char* json_parser_extract_string(JsonParser *parser, const char *key) {
    if (parser == NULL || key == NULL) return NULL;
    
    const char *key_pos = find_key_position(parser->json, key);
    if (key_pos == NULL) return NULL;
    
    // Move past the key and colon
    const char *value_start = strchr(key_pos, ':');
    if (value_start == NULL) return NULL;
    value_start++;
    
    // Skip whitespace
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }
    
    // Expect opening quote
    if (*value_start != '"') return NULL;
    value_start++;
    
    // Find closing quote, handling escaped quotes
    const char *value_end = value_start;
    while (*value_end != '\0') {
        if (*value_end == '"' && (value_end == value_start || *(value_end - 1) != '\\')) {
            break;
        }
        value_end++;
    }
    
    if (*value_end != '"') return NULL;
    
    // Extract and unescape the string
    size_t len = value_end - value_start;
    char *raw_value = malloc(len + 1);
    if (raw_value == NULL) return NULL;
    
    memcpy(raw_value, value_start, len);
    raw_value[len] = '\0';
    
    char *result = json_unescape_string(raw_value);
    free(raw_value);
    
    return result;
}

char* json_parser_extract_object(JsonParser *parser, const char *key) {
    if (parser == NULL || key == NULL) return NULL;
    
    const char *key_pos = find_key_position(parser->json, key);
    if (key_pos == NULL) return NULL;
    
    // Move past the key and colon
    const char *value_start = strchr(key_pos, ':');
    if (value_start == NULL) return NULL;
    value_start++;
    
    // Skip whitespace
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }
    
    // Expect opening brace
    if (*value_start != '{') return NULL;
    
    // Find matching closing brace
    const char *value_end = value_start + 1;
    int brace_count = 1;
    
    while (*value_end != '\0' && brace_count > 0) {
        if (*value_end == '{') {
            brace_count++;
        } else if (*value_end == '}') {
            brace_count--;
        }
        value_end++;
    }
    
    if (brace_count != 0) return NULL;
    
    // Extract the object
    size_t len = value_end - value_start;
    char *result = malloc(len + 1);
    if (result == NULL) return NULL;
    
    memcpy(result, value_start, len);
    result[len] = '\0';
    
    return result;
}

// Utility functions

char* json_escape_string(const char *str) {
    if (str == NULL) return strdup("");
    
    size_t len = strlen(str);
    // Worst case: every character needs escaping
    char *escaped = malloc(len * 2 + 1);
    if (escaped == NULL) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            case '\b':
                escaped[j++] = '\\';
                escaped[j++] = 'b';
                break;
            case '\f':
                escaped[j++] = '\\';
                escaped[j++] = 'f';
                break;
            default:
                if ((unsigned char)str[i] < 32) {
                    // Escape other control characters
                    sprintf(&escaped[j], "\\u%04x", (unsigned char)str[i]);
                    j += 6;
                } else {
                    escaped[j++] = str[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}

char* json_unescape_string(const char *str) {
    if (str == NULL) return NULL;
    
    size_t len = strlen(str);
    char *unescaped = malloc(len + 1);
    if (unescaped == NULL) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            switch (str[i + 1]) {
                case '"': unescaped[j++] = '"'; i++; break;
                case '\\': unescaped[j++] = '\\'; i++; break;
                case 'n': unescaped[j++] = '\n'; i++; break;
                case 'r': unescaped[j++] = '\r'; i++; break;
                case 't': unescaped[j++] = '\t'; i++; break;
                case 'b': unescaped[j++] = '\b'; i++; break;
                case 'f': unescaped[j++] = '\f'; i++; break;
                default: unescaped[j++] = str[i]; break;
            }
        } else {
            unescaped[j++] = str[i];
        }
    }
    unescaped[j] = '\0';
    
    return unescaped;
}

// Convenience functions

char* json_build_simple_object(const char *key1, const char *value1,
                               const char *key2, const char *value2) {
    JsonBuilder builder = {0};
    if (json_builder_init(&builder) != 0) return NULL;
    
    json_builder_start_object(&builder);
    json_builder_add_string(&builder, key1, value1);
    if (key2 && value2) {
        json_builder_add_separator(&builder);
        json_builder_add_string(&builder, key2, value2);
    }
    json_builder_end_object(&builder);
    
    char *result = json_builder_finalize(&builder);
    json_builder_cleanup(&builder);
    
    return result;
}

char* json_build_message(const char *role, const char *content) {
    return json_build_simple_object("role", role, "content", content);
}

char* json_build_tool_call(const char *id, const char *name, const char *arguments) {
    JsonBuilder builder = {0};
    JsonBuilder func_builder = {0};
    
    if (json_builder_init(&builder) != 0) return NULL;
    if (json_builder_init(&func_builder) != 0) {
        json_builder_cleanup(&builder);
        return NULL;
    }
    
    // Build function object
    json_builder_start_object(&func_builder);
    json_builder_add_string(&func_builder, "name", name);
    json_builder_add_separator(&func_builder);
    json_builder_add_string(&func_builder, "arguments", arguments);
    json_builder_end_object(&func_builder);
    
    char *function_json = json_builder_finalize(&func_builder);
    json_builder_cleanup(&func_builder);
    
    if (function_json == NULL) {
        json_builder_cleanup(&builder);
        return NULL;
    }
    
    // Build main tool call object
    json_builder_start_object(&builder);
    json_builder_add_string(&builder, "id", id);
    json_builder_add_separator(&builder);
    json_builder_add_string(&builder, "type", "function");
    json_builder_add_separator(&builder);
    json_builder_add_object(&builder, "function", function_json);
    json_builder_end_object(&builder);
    
    free(function_json);
    
    char *result = json_builder_finalize(&builder);
    json_builder_cleanup(&builder);
    
    return result;
}