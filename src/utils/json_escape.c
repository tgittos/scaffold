#include "json_escape.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* json_escape_string(const char* str) {
    if (str == NULL) return strdup("");
    
    cJSON *temp = cJSON_CreateString(str);
    if (temp == NULL) return NULL;
    
    char *json = cJSON_PrintUnformatted(temp);
    cJSON_Delete(temp);
    
    if (json == NULL) return NULL;
    
    // cJSON_PrintUnformatted wraps the value in JSON quotes; strip them
    size_t len = strlen(json);
    if (len >= 2 && json[0] == '"' && json[len-1] == '"') {
        memmove(json, json + 1, len - 2);
        json[len - 2] = '\0';
    }
    
    return json;
}