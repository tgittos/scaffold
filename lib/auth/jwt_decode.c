#include "jwt_decode.h"
#include <string.h>
#include <stdlib.h>
#include <mbedtls/base64.h>
#include <cJSON.h>

static int base64url_to_base64(const char *in, size_t in_len, char *out, size_t out_size) {
    if (in_len + 4 >= out_size) return -1;

    for (size_t i = 0; i < in_len; i++) {
        if (in[i] == '-') out[i] = '+';
        else if (in[i] == '_') out[i] = '/';
        else out[i] = in[i];
    }

    /* Add padding */
    size_t padded = in_len;
    size_t remainder = in_len % 4;
    if (remainder == 2) { out[padded++] = '='; out[padded++] = '='; }
    else if (remainder == 3) { out[padded++] = '='; }
    out[padded] = '\0';

    return (int)padded;
}

int jwt_extract_nested_claim(const char *jwt, const char *parent_key,
                              const char *child_key, char *out, size_t out_len) {
    if (!jwt || !parent_key || !child_key || !out || out_len == 0) return -1;

    /* Find payload segment (between first and second '.') */
    const char *dot1 = strchr(jwt, '.');
    if (!dot1) return -1;
    const char *payload_start = dot1 + 1;

    const char *dot2 = strchr(payload_start, '.');
    if (!dot2) return -1;

    size_t payload_b64url_len = (size_t)(dot2 - payload_start);
    if (payload_b64url_len == 0 || payload_b64url_len > 4096) return -1;

    /* Convert base64url to standard base64 */
    char b64[4100];
    int b64_len = base64url_to_base64(payload_start, payload_b64url_len, b64, sizeof(b64));
    if (b64_len < 0) return -1;

    /* Decode base64 */
    size_t decoded_len = 0;
    int rc = mbedtls_base64_decode(NULL, 0, &decoded_len,
                                    (const unsigned char *)b64, (size_t)b64_len);
    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || decoded_len == 0) return -1;

    unsigned char *decoded = malloc(decoded_len + 1);
    if (!decoded) return -1;

    rc = mbedtls_base64_decode(decoded, decoded_len + 1, &decoded_len,
                                (const unsigned char *)b64, (size_t)b64_len);
    if (rc != 0) { free(decoded); return -1; }
    decoded[decoded_len] = '\0';

    /* Parse JSON */
    cJSON *root = cJSON_ParseWithLength((const char *)decoded, decoded_len);
    free(decoded);
    if (!root) return -1;

    /* Navigate parent -> child */
    cJSON *parent = cJSON_GetObjectItem(root, parent_key);
    if (!parent || !cJSON_IsObject(parent)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *child = cJSON_GetObjectItem(parent, child_key);
    if (!child || !cJSON_IsString(child) || !child->valuestring) {
        cJSON_Delete(root);
        return -1;
    }

    size_t val_len = strlen(child->valuestring);
    if (val_len + 1 > out_len) {
        cJSON_Delete(root);
        return -1;
    }

    memcpy(out, child->valuestring, val_len + 1);
    cJSON_Delete(root);
    return 0;
}
