#include "common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

char* extract_string_param(const char *json, const char *param_name) {
    if (json == NULL || param_name == NULL) return NULL;
    
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start != '"') return NULL;
    start++; // Skip opening quote
    
    const char *end = start;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && *(end + 1) != '\0') {
            end += 2; // Skip escaped character
        } else {
            end++;
        }
    }
    
    if (*end != '"') return NULL;
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            switch (start[i + 1]) {
                case 'n': result[j++] = '\n'; i++; break;
                case 't': result[j++] = '\t'; i++; break;
                case 'r': result[j++] = '\r'; i++; break;
                case '"': result[j++] = '"'; i++; break;
                case '\\': result[j++] = '\\'; i++; break;
                default: result[j++] = start[i]; break;
            }
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';
    
    return result;
}

double extract_number_param(const char *json, const char *param_name, double default_value) {
    if (json == NULL || param_name == NULL) return default_value;
    
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return default_value;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    char *end;
    double value = strtod(start, &end);
    if (end == start) return default_value;
    
    return value;
}

int extract_array_numbers(const char *json, const char *param_name, float **out_array, size_t *out_size) {
    if (json == NULL || param_name == NULL || out_array == NULL || out_size == NULL) {
        return -1;
    }
    
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return -1;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start != '[') return -1;
    start++; // Skip '['
    
    size_t count = 0;
    const char *p = start;
    while (*p != ']' && *p != '\0') {
        char *end;
        strtod(p, &end);
        if (end > p) {
            count++;
            p = end;
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
        } else {
            p++;
        }
    }
    
    if (count == 0) return -1;
    
    float *array = malloc(count * sizeof(float));
    if (array == NULL) return -1;
    
    p = start;
    size_t i = 0;
    while (*p != ']' && *p != '\0' && i < count) {
        char *end;
        double value = strtod(p, &end);
        if (end > p) {
            array[i++] = (float)value;
            p = end;
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
        } else {
            p++;
        }
    }
    
    *out_array = array;
    *out_size = i;
    return 0;
}

char* create_error_message(const char *format, ...) {
    if (format == NULL) return NULL;
    
    va_list args;
    va_start(args, format);
    
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    if (needed < 0) {
        va_end(args);
        return NULL;
    }
    
    char *buffer = malloc(needed + 1);
    if (buffer == NULL) {
        va_end(args);
        return NULL;
    }
    
    vsnprintf(buffer, needed + 1, format, args);
    va_end(args);
    
    return buffer;
}

