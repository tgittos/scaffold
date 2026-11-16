#include "unity/unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUIDANCE "Use read_file (with start_line/end_line) to read files, " \
    "search_files to search contents, and list_dir to list or find files. " \
    "These never need approval. Use shell for builds, tests, git and other " \
    "commands; shell calls need approval."

void setUp(void) {}
void tearDown(void) {}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = calloc((size_t)n + 1, 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

static int count(const char *hay, const char *needle) {
    int c = 0;
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += strlen(needle)) c++;
    return c;
}

void test_system_prompt_has_guidance_once(void) {
    char *s = slurp("data/prompts/scaffold_system.txt");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(1, count(s, GUIDANCE));
    free(s);
}

void test_shell_description_has_guidance(void) {
    char *s = slurp("src/tools/python_defaults/shell.py");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(1, count(s, GUIDANCE));
    free(s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_system_prompt_has_guidance_once);
    RUN_TEST(test_shell_description_has_guidance);
    return UNITY_END();
}
