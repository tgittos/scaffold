#ifndef UUID_UTILS_H
#define UUID_UTILS_H

#define UUID_STRING_LEN 36

/**
 * Generate a UUID v4 (random) string.
 *
 * @param out_uuid Output buffer, must be at least 37 bytes (36 + null terminator)
 * @return 0 on success, -1 on failure
 */
int uuid_generate_v4(char* out_uuid);

/**
 * Validate a UUID string format.
 *
 * @param uuid_str The UUID string to validate
 * @return 1 if valid, 0 if invalid
 */
int uuid_is_valid(const char* uuid_str);

#endif // UUID_UTILS_H
