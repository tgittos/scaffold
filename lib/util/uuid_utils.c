#include "uuid_utils.h"
#include <uuid.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int uuid_generate_v4(char* out_uuid) {
    if (out_uuid == NULL) {
        return -1;
    }

    uuid_t* uuid = NULL;
    uuid_rc_t rc;

    rc = uuid_create(&uuid);
    if (rc != UUID_RC_OK) {
        return -1;
    }

    rc = uuid_make(uuid, UUID_MAKE_V4);
    if (rc != UUID_RC_OK) {
        uuid_destroy(uuid);
        return -1;
    }

    char* str = NULL;
    size_t len = UUID_LEN_STR + 1;
    rc = uuid_export(uuid, UUID_FMT_STR, &str, &len);
    if (rc != UUID_RC_OK || str == NULL) {
        uuid_destroy(uuid);
        return -1;
    }

    strncpy(out_uuid, str, UUID_STRING_LEN);
    out_uuid[UUID_STRING_LEN] = '\0';

    free(str);
    uuid_destroy(uuid);

    return 0;
}

int uuid_is_valid(const char* uuid_str) {
    if (uuid_str == NULL) {
        return 0;
    }

    size_t len = strlen(uuid_str);
    if (len != UUID_STRING_LEN) {
        return 0;
    }

    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    // Positions:   01234567-9012-4567-9012-456789012345
    //              ^       ^    ^    ^    ^
    //              8       13   18   23   (hyphens)
    for (size_t i = 0; i < len; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid_str[i] != '-') {
                return 0;
            }
        } else {
            if (!isxdigit((unsigned char)uuid_str[i])) {
                return 0;
            }
        }
    }

    return 1;
}
