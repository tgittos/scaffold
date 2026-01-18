#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../unity/unity.h"
#include "../../src/utils/debug_output.h"

void setUp(void) {
    debug_init(true);
}

void tearDown(void) {
}

void test_debug_summarize_json_null(void) {
    char *result = debug_summarize_json(NULL);
    TEST_ASSERT_NULL(result);
}

void test_debug_summarize_json_invalid(void) {
    char *result = debug_summarize_json("not valid json");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("not valid json", result);
    free(result);
}

void test_debug_summarize_json_simple_object(void) {
    const char *json = "{\"name\":\"test\",\"value\":42}";
    char *result = debug_summarize_json(json);
    TEST_ASSERT_NOT_NULL(result);
    // Should contain the original data
    TEST_ASSERT_NOT_NULL(strstr(result, "\"name\""));
    TEST_ASSERT_NOT_NULL(strstr(result, "test"));
    TEST_ASSERT_NOT_NULL(strstr(result, "42"));
    free(result);
}

void test_debug_summarize_json_small_numeric_array(void) {
    // Arrays with 10 or fewer numbers should be preserved
    const char *json = "{\"data\":[1.0,2.0,3.0,4.0,5.0]}";
    char *result = debug_summarize_json(json);
    TEST_ASSERT_NOT_NULL(result);
    // Should still contain all original numbers
    TEST_ASSERT_NOT_NULL(strstr(result, "1"));
    TEST_ASSERT_NOT_NULL(strstr(result, "5"));
    free(result);
}

void test_debug_summarize_json_large_numeric_array(void) {
    // Build a JSON with a large numeric array (simulating embeddings)
    char *large_json = malloc(10000);
    strcpy(large_json, "{\"embedding\":[");
    for (int i = 0; i < 100; i++) {
        char num[20];
        snprintf(num, sizeof(num), "%s0.%04d", i > 0 ? "," : "", i);
        strcat(large_json, num);
    }
    strcat(large_json, "]}");

    char *result = debug_summarize_json(large_json);
    TEST_ASSERT_NOT_NULL(result);

    // Should contain the summary marker
    TEST_ASSERT_NOT_NULL(strstr(result, "100 floats"));

    // Should NOT contain all the original numbers
    TEST_ASSERT_NULL(strstr(result, "0.0050"));

    free(result);
    free(large_json);
}

void test_debug_summarize_json_nested_arrays(void) {
    // Build nested structure with large array
    char *json = malloc(5000);
    strcpy(json, "{\"object\":{\"data\":{\"embedding\":[");
    for (int i = 0; i < 50; i++) {
        char num[20];
        snprintf(num, sizeof(num), "%s1.%02d", i > 0 ? "," : "", i);
        strcat(json, num);
    }
    strcat(json, "]}}}");

    char *result = debug_summarize_json(json);
    TEST_ASSERT_NOT_NULL(result);

    // Should contain the summary marker
    TEST_ASSERT_NOT_NULL(strstr(result, "50 floats"));

    free(result);
    free(json);
}

void test_debug_summarize_json_mixed_array(void) {
    // Arrays with non-numeric values should be preserved
    const char *json = "{\"data\":[\"hello\",\"world\"]}";
    char *result = debug_summarize_json(json);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(strstr(result, "hello"));
    TEST_ASSERT_NOT_NULL(strstr(result, "world"));
    free(result);
}

void test_debug_summarize_json_api_response(void) {
    // Simulate a real embeddings API response
    char *json = malloc(20000);
    strcpy(json, "{\"object\":\"list\",\"data\":[{\"object\":\"embedding\",\"index\":0,\"embedding\":[");
    for (int i = 0; i < 1536; i++) {  // text-embedding-3-small has 1536 dimensions
        char num[30];
        snprintf(num, sizeof(num), "%s-0.00%04d", i > 0 ? "," : "", i);
        strcat(json, num);
    }
    strcat(json, "]}],\"model\":\"text-embedding-3-small\",\"usage\":{\"prompt_tokens\":5,\"total_tokens\":5}}");

    char *result = debug_summarize_json(json);
    TEST_ASSERT_NOT_NULL(result);

    // Should contain summary of 1536 floats
    TEST_ASSERT_NOT_NULL(strstr(result, "1536 floats"));

    // Should still contain metadata
    TEST_ASSERT_NOT_NULL(strstr(result, "text-embedding-3-small"));
    TEST_ASSERT_NOT_NULL(strstr(result, "prompt_tokens"));

    free(result);
    free(json);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_debug_summarize_json_null);
    RUN_TEST(test_debug_summarize_json_invalid);
    RUN_TEST(test_debug_summarize_json_simple_object);
    RUN_TEST(test_debug_summarize_json_small_numeric_array);
    RUN_TEST(test_debug_summarize_json_large_numeric_array);
    RUN_TEST(test_debug_summarize_json_nested_arrays);
    RUN_TEST(test_debug_summarize_json_mixed_array);
    RUN_TEST(test_debug_summarize_json_api_response);

    return UNITY_END();
}
