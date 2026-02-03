/*
 * tool_param_dsl.c - Table-driven tool parameter registration implementation
 */

#include "tool_param_dsl.h"
#include <stdlib.h>
#include <string.h>

int count_enum_values(const char **enum_values) {
    if (enum_values == NULL) {
        return 0;
    }
    int count = 0;
    while (enum_values[count] != NULL) {
        count++;
    }
    return count;
}

/*
 * Helper to duplicate enum values array.
 * Returns NULL on failure or if source is NULL.
 */
static char **dup_enum_values(const char **enum_values, int count) {
    if (enum_values == NULL || count == 0) {
        return NULL;
    }

    char **result = calloc((size_t)count, sizeof(char *));
    if (result == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        result[i] = strdup(enum_values[i]);
        if (result[i] == NULL) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                free(result[j]);
            }
            free(result);
            return NULL;
        }
    }

    return result;
}

/*
 * Helper to free a ToolParameter's dynamically allocated fields.
 */
static void free_tool_parameter(ToolParameter *param) {
    if (param == NULL) {
        return;
    }
    free(param->name);
    free(param->type);
    free(param->description);
    if (param->enum_values != NULL) {
        for (int i = 0; i < param->enum_count; i++) {
            free(param->enum_values[i]);
        }
        free(param->enum_values);
    }
    param->name = NULL;
    param->type = NULL;
    param->description = NULL;
    param->enum_values = NULL;
    param->enum_count = 0;
}

/*
 * Helper to cleanup an array of ToolParameters.
 */
static void cleanup_parameters(ToolParameter *params, int count) {
    for (int i = 0; i < count; i++) {
        free_tool_parameter(&params[i]);
    }
}

int register_tool_from_def(ToolRegistry *registry, const ToolDef *def) {
    if (registry == NULL || def == NULL || def->name == NULL || def->execute == NULL) {
        return -1;
    }

    /* Handle tools with no parameters */
    if (def->params == NULL || def->param_count == 0) {
        return register_tool(registry, def->name, def->description, NULL, 0,
                             def->execute);
    }

    /* Allocate and populate ToolParameter array */
    ToolParameter *params = calloc((size_t)def->param_count, sizeof(ToolParameter));
    if (params == NULL) {
        return -1;
    }

    for (int i = 0; i < def->param_count; i++) {
        const ParamDef *pd = &def->params[i];

        params[i].name = strdup(pd->name);
        params[i].type = strdup(pd->type);
        params[i].description = strdup(pd->description);
        params[i].required = pd->required;

        if (params[i].name == NULL || params[i].type == NULL ||
            params[i].description == NULL) {
            cleanup_parameters(params, i + 1);
            free(params);
            return -1;
        }

        /* Handle enum values */
        int enum_count = count_enum_values(pd->enum_values);
        if (enum_count > 0) {
            params[i].enum_values = dup_enum_values(pd->enum_values, enum_count);
            if (params[i].enum_values == NULL) {
                cleanup_parameters(params, i + 1);
                free(params);
                return -1;
            }
            params[i].enum_count = enum_count;
        } else {
            params[i].enum_values = NULL;
            params[i].enum_count = 0;
        }
    }

    /* Register the tool */
    int result = register_tool(registry, def->name, def->description, params,
                               def->param_count, def->execute);

    /* Cleanup - register_tool copies the parameters */
    cleanup_parameters(params, def->param_count);
    free(params);

    return result;
}

int register_tools_from_defs(ToolRegistry *registry, const ToolDef *defs, int count) {
    if (registry == NULL || defs == NULL || count <= 0) {
        return 0;
    }

    int registered = 0;
    for (int i = 0; i < count; i++) {
        if (register_tool_from_def(registry, &defs[i]) != 0) {
            return registered;
        }
        registered++;
    }

    return registered;
}
