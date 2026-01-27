#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <stddef.h>

/**
 * Safe string duplication that handles NULL input
 * 
 * @param str String to duplicate (can be NULL)
 * @return Duplicated string or NULL if input is NULL or allocation fails
 */
char* safe_strdup(const char *str);

/**
 * Extract string parameter from JSON
 * 
 * @param json JSON string to parse
 * @param param_name Parameter name to extract
 * @return Extracted string (caller must free) or NULL if not found
 */
char* extract_string_param(const char *json, const char *param_name);

/**
 * Extract numeric parameter from JSON
 * 
 * @param json JSON string to parse
 * @param param_name Parameter name to extract
 * @param default_value Default value if parameter not found
 * @return Extracted number or default_value
 */
double extract_number_param(const char *json, const char *param_name, double default_value);

/**
 * Extract array of numbers from JSON
 * 
 * @param json JSON string to parse
 * @param param_name Parameter name to extract
 * @param out_array Output array (caller must free)
 * @param out_size Output array size
 * @return 0 on success, -1 on failure
 */
int extract_array_numbers(const char *json, const char *param_name, float **out_array, size_t *out_size);

/**
 * Create formatted error message
 * 
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return Formatted error message (caller must free)
 */
char* create_error_message(const char *format, ...);

#endif // COMMON_UTILS_H