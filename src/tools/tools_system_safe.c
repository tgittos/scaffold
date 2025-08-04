// Safe replacement for tools_system.c buffer overflow functions
#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Safe JSON builder with bounds checking
typedef struct {
    char *buffer;
    size_t size;
    size_t pos;
} SafeJsonBuilder;

static int safe_json_init(SafeJsonBuilder *builder, size_t initial_size) {
    builder->buffer = malloc(initial_size);
    if (!builder->buffer) return -1;
    builder->size = initial_size;
    builder->pos = 0;
    builder->buffer[0] = '\0';
    return 0;
}

static int safe_json_append(SafeJsonBuilder *builder, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    size_t remaining = builder->size - builder->pos;
    int needed = vsnprintf(builder->buffer + builder->pos, remaining, format, args);
    va_end(args);
    
    if (needed < 0) return -1;
    
    if ((size_t)needed >= remaining) {
        // Need to grow buffer
        size_t new_size = builder->size * 2;
        while (new_size - builder->pos <= (size_t)needed) {
            new_size *= 2;
        }
        
        char *new_buffer = realloc(builder->buffer, new_size);
        if (!new_buffer) return -1;
        
        builder->buffer = new_buffer;
        builder->size = new_size;
        
        va_start(args, format);
        needed = vsnprintf(builder->buffer + builder->pos, builder->size - builder->pos, format, args);
        va_end(args);
        
        if (needed < 0) return -1;
    }
    
    builder->pos += needed;
    return 0;
}

static char* safe_json_finalize(SafeJsonBuilder *builder) {
    return builder->buffer;
}

// Safe tools JSON generation
char* generate_tools_json_safe(const ToolRegistry *registry) {
    if (registry == NULL || registry->function_count == 0) {
        char *empty = malloc(3);
        if (empty) strcpy(empty, "[]");
        return empty;
    }
    
    SafeJsonBuilder builder;
    if (safe_json_init(&builder, 4096) != 0) {
        return NULL;
    }
    
    if (safe_json_append(&builder, "[") != 0) {
        free(builder.buffer);
        return NULL;
    }
    
    for (int i = 0; i < registry->function_count; i++) {
        const ToolFunction *func = &registry->functions[i];
        
        if (i > 0) {
            if (safe_json_append(&builder, ", ") != 0) {
                free(builder.buffer);
                return NULL;
            }
        }
        
        if (safe_json_append(&builder, 
            "{\"type\": \"function\", \"function\": {\"name\": \"%.200s\", \"description\": \"%.500s\"",
            func->name, func->description) != 0) {
            free(builder.buffer);
            return NULL;
        }
        
        if (func->parameter_count > 0) {
            if (safe_json_append(&builder, ", \"parameters\": {\"type\": \"object\", \"properties\": {") != 0) {
                free(builder.buffer);
                return NULL;
            }
            
            for (int j = 0; j < func->parameter_count; j++) {
                const ToolParameter *param = &func->parameters[j];
                
                if (j > 0) {
                    if (safe_json_append(&builder, ", ") != 0) {
                        free(builder.buffer);
                        return NULL;
                    }
                }
                
                if (safe_json_append(&builder, 
                    "\"%.100s\": {\"type\": \"%.50s\", \"description\": \"%.500s\"",
                    param->name, param->type, param->description) != 0) {
                    free(builder.buffer);
                    return NULL;
                }
                
                if (strcmp(param->type, "array") == 0) {
                    if (safe_json_append(&builder, ", \"items\": {\"type\": \"object\"}") != 0) {
                        free(builder.buffer);
                        return NULL;
                    }
                }
                
                if (param->enum_values != NULL && param->enum_count > 0) {
                    if (safe_json_append(&builder, ", \"enum\": [") != 0) {
                        free(builder.buffer);
                        return NULL;
                    }
                    
                    for (int k = 0; k < param->enum_count; k++) {
                        if (k > 0) {
                            if (safe_json_append(&builder, ", ") != 0) {
                                free(builder.buffer);
                                return NULL;
                            }
                        }
                        if (safe_json_append(&builder, "\"%.100s\"", param->enum_values[k]) != 0) {
                            free(builder.buffer);
                            return NULL;
                        }
                    }
                    
                    if (safe_json_append(&builder, "]") != 0) {
                        free(builder.buffer);
                        return NULL;
                    }
                }
                
                if (safe_json_append(&builder, "}") != 0) {
                    free(builder.buffer);
                    return NULL;
                }
            }
            
            if (safe_json_append(&builder, "}, \"required\": [") != 0) {
                free(builder.buffer);
                return NULL;
            }
            
            int first_required = 1;
            for (int j = 0; j < func->parameter_count; j++) {
                if (func->parameters[j].required) {
                    if (!first_required) {
                        if (safe_json_append(&builder, ", ") != 0) {
                            free(builder.buffer);
                            return NULL;
                        }
                    }
                    if (safe_json_append(&builder, "\"%.100s\"", func->parameters[j].name) != 0) {
                        free(builder.buffer);
                        return NULL;
                    }
                    first_required = 0;
                }
            }
            
            if (safe_json_append(&builder, "]}") != 0) {
                free(builder.buffer);
                return NULL;
            }
        }
        
        if (safe_json_append(&builder, "}}") != 0) {
            free(builder.buffer);
            return NULL;
        }
    }
    
    if (safe_json_append(&builder, "]") != 0) {
        free(builder.buffer);
        return NULL;
    }
    
    return safe_json_finalize(&builder);
}