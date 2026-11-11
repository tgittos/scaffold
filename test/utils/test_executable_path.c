#include "../unity/unity.h"
#include "util/executable_path.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char original_cwd[PATH_MAX];

void setUp(void) {
    TEST_ASSERT_NOT_NULL(getcwd(original_cwd, sizeof(original_cwd)));
}

void tearDown(void) {
    TEST_ASSERT_EQUAL(0, chdir(original_cwd));
}

static void test_resolves_running_binary_from_another_directory(void) {
    TEST_ASSERT_EQUAL(0, chdir("/tmp"));

    char *path = get_executable_path();
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_CHAR('/', path[0]);
    TEST_ASSERT_EQUAL(0, access(path, X_OK));

    const char *base = strrchr(path, '/') + 1;
    TEST_ASSERT_EQUAL(0, strncmp(base, "test_executable_path", strlen("test_executable_path")));

    free(path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_resolves_running_binary_from_another_directory);
    return UNITY_END();
}
