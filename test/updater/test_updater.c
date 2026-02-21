#include "unity.h"
#include "../../lib/updater/updater.h"
#include "build/version.h"
#include <string.h>

extern void mock_http_set_response(const char *json, int error_code);

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================= */
/* parse_semver tests                                                        */
/* ========================================================================= */

void test_parse_semver_valid(void) {
    int major, minor, patch;
    TEST_ASSERT_EQUAL_INT(0, parse_semver("v1.2.3", &major, &minor, &patch));
    TEST_ASSERT_EQUAL_INT(1, major);
    TEST_ASSERT_EQUAL_INT(2, minor);
    TEST_ASSERT_EQUAL_INT(3, patch);
}

void test_parse_semver_no_prefix(void) {
    int major, minor, patch;
    TEST_ASSERT_EQUAL_INT(0, parse_semver("1.2.3", &major, &minor, &patch));
    TEST_ASSERT_EQUAL_INT(1, major);
    TEST_ASSERT_EQUAL_INT(2, minor);
    TEST_ASSERT_EQUAL_INT(3, patch);
}

void test_parse_semver_invalid(void) {
    int major, minor, patch;
    TEST_ASSERT_EQUAL_INT(-1, parse_semver("vX.Y.Z", &major, &minor, &patch));
    TEST_ASSERT_EQUAL_INT(-1, parse_semver("", &major, &minor, &patch));
    TEST_ASSERT_EQUAL_INT(-1, parse_semver(NULL, &major, &minor, &patch));
}

/* ========================================================================= */
/* semver_compare tests                                                      */
/* ========================================================================= */

void test_semver_compare_equal(void) {
    TEST_ASSERT_EQUAL_INT(0, semver_compare(1, 2, 3, 1, 2, 3));
}

void test_semver_compare_major(void) {
    TEST_ASSERT_EQUAL_INT(1, semver_compare(2, 0, 0, 1, 9, 9));
}

void test_semver_compare_minor(void) {
    TEST_ASSERT_EQUAL_INT(1, semver_compare(1, 3, 0, 1, 2, 9));
}

void test_semver_compare_patch(void) {
    TEST_ASSERT_EQUAL_INT(1, semver_compare(1, 2, 4, 1, 2, 3));
}

void test_semver_compare_less(void) {
    TEST_ASSERT_EQUAL_INT(-1, semver_compare(0, 9, 0, 1, 0, 0));
}

/* ========================================================================= */
/* updater_check integration tests (with mock HTTP)                          */
/* ========================================================================= */

#define RELEASE_JSON_TEMPLATE \
    "{\"tag_name\":\"%s\",\"prerelease\":false,\"body\":\"Release notes\"," \
    "\"assets\":[{\"name\":\"scaffold\",\"browser_download_url\":\"https://example.com/scaffold\"," \
    "\"size\":12345}]}"

static char json_buf[2048];

void test_check_update_available(void) {
    snprintf(json_buf, sizeof(json_buf), RELEASE_JSON_TEMPLATE, "v99.0.0");
    mock_http_set_response(json_buf, 0);

    updater_release_t release;
    updater_status_t status = updater_check(&release);

    TEST_ASSERT_EQUAL_INT(UPDATE_AVAILABLE, status);
    TEST_ASSERT_EQUAL_INT(99, release.major);
    TEST_ASSERT_EQUAL_INT(0, release.minor);
    TEST_ASSERT_EQUAL_INT(0, release.patch);
    TEST_ASSERT_EQUAL_STRING("v99.0.0", release.tag);
    TEST_ASSERT_EQUAL_STRING("https://example.com/scaffold", release.download_url);
    TEST_ASSERT_EQUAL(12345, release.asset_size);
}

void test_check_up_to_date(void) {
    /* Use current compiled version - should be equal, not an update */
    char tag[32];
    snprintf(tag, sizeof(tag), "v%d.%d.%d",
             RALPH_VERSION_MAJOR, RALPH_VERSION_MINOR, RALPH_VERSION_PATCH);
    snprintf(json_buf, sizeof(json_buf), RELEASE_JSON_TEMPLATE, tag);
    mock_http_set_response(json_buf, 0);

    updater_release_t release;
    updater_status_t status = updater_check(&release);

    TEST_ASSERT_EQUAL_INT(UP_TO_DATE, status);
}

void test_check_network_failure(void) {
    mock_http_set_response(NULL, -1);

    updater_release_t release;
    updater_status_t status = updater_check(&release);

    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, status);
}

void test_check_malformed_json(void) {
    mock_http_set_response("{not valid json at all", 0);

    updater_release_t release;
    updater_status_t status = updater_check(&release);

    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, status);
}

void test_check_null_release(void) {
    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, updater_check(NULL));
}

void test_check_missing_asset(void) {
    const char *json =
        "{\"tag_name\":\"v99.0.0\",\"prerelease\":false,\"body\":\"notes\","
        "\"assets\":[]}";
    mock_http_set_response(json, 0);

    updater_release_t release;
    updater_status_t status = updater_check(&release);

    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, status);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_semver_valid);
    RUN_TEST(test_parse_semver_no_prefix);
    RUN_TEST(test_parse_semver_invalid);
    RUN_TEST(test_semver_compare_equal);
    RUN_TEST(test_semver_compare_major);
    RUN_TEST(test_semver_compare_minor);
    RUN_TEST(test_semver_compare_patch);
    RUN_TEST(test_semver_compare_less);
    RUN_TEST(test_check_null_release);
    RUN_TEST(test_check_update_available);
    RUN_TEST(test_check_up_to_date);
    RUN_TEST(test_check_network_failure);
    RUN_TEST(test_check_malformed_json);
    RUN_TEST(test_check_missing_asset);

    return UNITY_END();
}
