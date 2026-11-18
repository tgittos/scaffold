#include "unity/unity.h"
#include "util/stdin_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

/* Build a 10,000-line string and write it to a tmpfile, then read it back
 * and verify byte-for-byte identity. */
void test_read_all_multiline(void) {
    /* Build expected content */
    const int lines = 10000;
    /* Each line is "line NNNNN\n" = up to 12 chars */
    size_t buf_size = (size_t)lines * 16;
    char *expected = malloc(buf_size);
    TEST_ASSERT_NOT_NULL(expected);

    size_t pos = 0;
    for (int i = 1; i <= lines; i++) {
        int n = snprintf(expected + pos, buf_size - pos, "line %d\n", i);
        pos += (size_t)n;
    }
    expected[pos] = '\0';
    size_t expected_len = pos;

    /* Write to a tmpfile */
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    size_t written = fwrite(expected, 1, expected_len, f);
    TEST_ASSERT_EQUAL_size_t(expected_len, written);
    rewind(f);

    /* Read back */
    int truncated = 0;
    char *result = stdin_read_all(f, 8 * 1024 * 1024, &truncated);
    fclose(f);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, truncated);
    TEST_ASSERT_EQUAL_size_t(expected_len, strlen(result));
    TEST_ASSERT_EQUAL_STRING(expected, result);

    free(result);
    free(expected);
}

/* When input exceeds max_bytes, returns NULL and sets truncated = 1. */
void test_read_all_over_limit(void) {
    /* Write 100 bytes to tmpfile but set limit to 50 */
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    char data[100];
    memset(data, 'x', sizeof(data));
    fwrite(data, 1, sizeof(data), f);
    rewind(f);

    int truncated = 0;
    char *result = stdin_read_all(f, 50, &truncated);
    fclose(f);

    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, truncated);
}

/* NULL input → NULL result, truncated stays 0. */
void test_read_all_null_input(void) {
    int truncated = 0;
    char *result = stdin_read_all(NULL, 8 * 1024 * 1024, &truncated);
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, truncated);
}

/* Empty file returns empty string (not NULL). */
void test_read_all_empty_file(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);

    int truncated = 0;
    char *result = stdin_read_all(f, 8 * 1024 * 1024, &truncated);
    fclose(f);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, truncated);
    TEST_ASSERT_EQUAL_size_t(0, strlen(result));

    free(result);
}

/* When an I/O error occurs (ferror), returns NULL with truncated == 0,
 * distinct from the over-limit case (truncated == 1). */
void test_read_all_ferror_returns_null_not_truncated(void) {
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    /* Write some data so the read has content to process */
    char data[128];
    memset(data, 'a', sizeof(data));
    fwrite(data, 1, sizeof(data), f);
    rewind(f);
    /* Close the underlying fd to trigger ferror on the next fread */
    close(fileno(f));

    int truncated = -1;
    char *result = stdin_read_all(f, 8 * 1024 * 1024, &truncated);
    /* Don't fclose(f) — fd is already closed */
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, truncated); /* not truncation — I/O error */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_all_multiline);
    RUN_TEST(test_read_all_over_limit);
    RUN_TEST(test_read_all_null_input);
    RUN_TEST(test_read_all_empty_file);
    RUN_TEST(test_read_all_ferror_returns_null_not_truncated);
    return UNITY_END();
}
