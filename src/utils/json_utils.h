#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stddef.h>

/**
 * JsonBuilder - Safe JSON construction utility
 * Eliminates buffer overflow risks from manual string concatenation
 */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    int error;  // Error flag for failed operations
} JsonBuilder;

/**
 * JsonParser - Unified JSON parsing utility
 * Replaces duplicate parsing logic across the codebase
 */
typedef struct {
    const char *json;
    size_t length;
    size_t position;
} JsonParser;

// JsonBuilder functions
int json_builder_init(JsonBuilder *builder);
int json_builder_start_object(JsonBuilder *builder);
int json_builder_end_object(JsonBuilder *builder);
int json_builder_start_array(JsonBuilder *builder);
int json_builder_end_array(JsonBuilder *builder);
int json_builder_add_string(JsonBuilder *builder, const char *key, const char *value);
int json_builder_add_string_no_key(JsonBuilder *builder, const char *value);
int json_builder_add_object(JsonBuilder *builder, const char *key, const char *json);
int json_builder_add_object_no_key(JsonBuilder *builder, const char *json);
int json_builder_add_integer(JsonBuilder *builder, const char *key, int value);
int json_builder_add_boolean(JsonBuilder *builder, const char *key, int value);
int json_builder_add_separator(JsonBuilder *builder);
char* json_builder_finalize(JsonBuilder *builder);
void json_builder_cleanup(JsonBuilder *builder);

// JsonParser functions
int json_parser_init(JsonParser *parser, const char *json);
char* json_parser_extract_string(JsonParser *parser, const char *key);
char* json_parser_extract_object(JsonParser *parser, const char *key);
int json_parser_extract_integer(JsonParser *parser, const char *key, int *value);
int json_parser_extract_boolean(JsonParser *parser, const char *key, int *value);
char* json_parser_extract_array_item(JsonParser *parser, int index);

// Utility functions
char* json_escape_string(const char *str);
char* json_unescape_string(const char *str);
int json_validate(const char *json);

// Convenience functions for common patterns
char* json_build_simple_object(const char *key1, const char *value1, 
                               const char *key2, const char *value2);
char* json_build_message(const char *role, const char *content);
char* json_build_tool_call(const char *id, const char *name, const char *arguments);

#endif // JSON_UTILS_H