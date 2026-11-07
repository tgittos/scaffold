#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include "debug_output.h"

#define LARGE_ARRAY_THRESHOLD 10

static bool is_numeric_array(const cJSON *array) {
    if (!cJSON_IsArray(array)) {
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsNumber(item)) {
            return false;
        }
        count++;
    }
    return count > 0;
}

// Replaces large numeric arrays (e.g. embeddings) with a compact summary
// to keep debug output readable.
static void summarize_json_recursive(cJSON *node) {
    if (node == NULL) {
        return;
    }

    if (cJSON_IsArray(node) && is_numeric_array(node)) {
        int count = cJSON_GetArraySize(node);
        if (count > LARGE_ARRAY_THRESHOLD) {
            cJSON *first = cJSON_GetArrayItem(node, 0);
            cJSON *last = cJSON_GetArrayItem(node, count - 1);
            double first_val = first ? first->valuedouble : 0.0;
            double last_val = last ? last->valuedouble : 0.0;

            while (cJSON_GetArraySize(node) > 0) {
                cJSON_DeleteItemFromArray(node, 0);
            }

            char summary[128] = {0};
            snprintf(summary, sizeof(summary),
                     "<%d floats: %.4f ... %.4f>", count, first_val, last_val);
            cJSON_AddItemToArray(node, cJSON_CreateString(summary));
        }
        return;
    }

    if (cJSON_IsObject(node)) {
        cJSON *child = node->child;
        while (child != NULL) {
            summarize_json_recursive(child);
            child = child->next;
        }
    } else if (cJSON_IsArray(node)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, node) {
            summarize_json_recursive(item);
        }
    }
}

char* debug_summarize_json(const char *json) {
    if (json == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return strdup(json);
    }

    summarize_json_recursive(root);

    char *result = cJSON_Print(root);
    cJSON_Delete(root);

    return result;
}
