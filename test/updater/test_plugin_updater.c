#include "unity.h"
#include "../../lib/updater/updater.h"
#include <string.h>

extern void mock_http_set_response(const char *json, int error_code);

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================= */
/* updater_check_plugins tests                                               */
/* ========================================================================= */

#define RELEASES_JSON_TWO_PLUGINS \
    "[{\"tag_name\":\"plugin-session-transcript-v1.2.0\"," \
    "\"body\":\"Plugin release\"," \
    "\"assets\":[{\"name\":\"session-transcript\"," \
    "\"browser_download_url\":\"https://example.com/session-transcript\"," \
    "\"size\":54321}]}," \
    "{\"tag_name\":\"v0.3.0\"," \
    "\"body\":\"Scaffold release\"," \
    "\"assets\":[{\"name\":\"scaffold\"," \
    "\"browser_download_url\":\"https://example.com/scaffold\"," \
    "\"size\":99999}]}," \
    "{\"tag_name\":\"plugin-my-plugin-v2.0.0\"," \
    "\"body\":\"My plugin release\"," \
    "\"assets\":[{\"name\":\"my-plugin\"," \
    "\"browser_download_url\":\"https://example.com/my-plugin\"," \
    "\"size\":11111}]}]"

void test_plugin_update_available(void) {
    mock_http_set_response(RELEASES_JSON_TWO_PLUGINS, 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(UPDATE_AVAILABLE, statuses[0]);
    TEST_ASSERT_EQUAL_INT(1, results[0].major);
    TEST_ASSERT_EQUAL_INT(2, results[0].minor);
    TEST_ASSERT_EQUAL_INT(0, results[0].patch);
    TEST_ASSERT_EQUAL_STRING("plugin-session-transcript-v1.2.0", results[0].tag);
    TEST_ASSERT_EQUAL_STRING("https://example.com/session-transcript",
                             results[0].download_url);
    TEST_ASSERT_EQUAL(54321, results[0].asset_size);
}

void test_plugin_up_to_date(void) {
    mock_http_set_response(RELEASES_JSON_TWO_PLUGINS, 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.2.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(UP_TO_DATE, statuses[0]);
}

void test_plugin_newer_than_release(void) {
    mock_http_set_response(RELEASES_JSON_TWO_PLUGINS, 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "2.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(UP_TO_DATE, statuses[0]);
}

void test_plugin_tag_matching_ignores_scaffold_tags(void) {
    /* The v0.3.0 tag in the releases should NOT match any plugin */
    mock_http_set_response(RELEASES_JSON_TWO_PLUGINS, 0);

    plugin_version_info_t plugins[] = {
        { .name = "my-plugin", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(UPDATE_AVAILABLE, statuses[0]);
    TEST_ASSERT_EQUAL_INT(2, results[0].major);
    TEST_ASSERT_EQUAL_STRING("https://example.com/my-plugin", results[0].download_url);
}

void test_plugin_multiple_plugins_batched(void) {
    mock_http_set_response(RELEASES_JSON_TWO_PLUGINS, 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" },
        { .name = "my-plugin", .version = "1.0.0" }
    };
    updater_release_t results[2];
    updater_status_t statuses[2];

    updater_check_plugins(plugins, 2, results, statuses);

    TEST_ASSERT_EQUAL_INT(UPDATE_AVAILABLE, statuses[0]);
    TEST_ASSERT_EQUAL_INT(1, results[0].major);
    TEST_ASSERT_EQUAL_INT(2, results[0].minor);

    TEST_ASSERT_EQUAL_INT(UPDATE_AVAILABLE, statuses[1]);
    TEST_ASSERT_EQUAL_INT(2, results[1].major);
    TEST_ASSERT_EQUAL_INT(0, results[1].minor);
}

void test_plugin_no_matching_release(void) {
    mock_http_set_response(RELEASES_JSON_TWO_PLUGINS, 0);

    plugin_version_info_t plugins[] = {
        { .name = "nonexistent-plugin", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    /* No matching release = up to date (no release exists yet) */
    TEST_ASSERT_EQUAL_INT(UP_TO_DATE, statuses[0]);
}

void test_plugin_network_failure(void) {
    mock_http_set_response(NULL, -1);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, statuses[0]);
}

void test_plugin_empty_releases(void) {
    mock_http_set_response("[]", 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(UP_TO_DATE, statuses[0]);
}

void test_plugin_malformed_json(void) {
    mock_http_set_response("{not valid json", 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, statuses[0]);
}

void test_plugin_null_inputs(void) {
    updater_release_t results[1];
    updater_status_t statuses[1];

    /* Should not crash */
    updater_check_plugins(NULL, 1, results, statuses);
    updater_check_plugins(NULL, 0, NULL, NULL);
}

void test_plugin_zero_count(void) {
    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    /* Should not crash or make HTTP call */
    updater_check_plugins(plugins, 0, results, statuses);
}

void test_plugin_missing_asset(void) {
    /* Release exists but has no matching asset */
    const char *json =
        "[{\"tag_name\":\"plugin-session-transcript-v2.0.0\","
        "\"body\":\"Release\","
        "\"assets\":[{\"name\":\"wrong-name\","
        "\"browser_download_url\":\"https://example.com/wrong\","
        "\"size\":12345}]}]";
    mock_http_set_response(json, 0);

    plugin_version_info_t plugins[] = {
        { .name = "session-transcript", .version = "1.0.0" }
    };
    updater_release_t results[1];
    updater_status_t statuses[1];

    updater_check_plugins(plugins, 1, results, statuses);

    TEST_ASSERT_EQUAL_INT(CHECK_FAILED, statuses[0]);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_plugin_update_available);
    RUN_TEST(test_plugin_up_to_date);
    RUN_TEST(test_plugin_newer_than_release);
    RUN_TEST(test_plugin_tag_matching_ignores_scaffold_tags);
    RUN_TEST(test_plugin_multiple_plugins_batched);
    RUN_TEST(test_plugin_no_matching_release);
    RUN_TEST(test_plugin_network_failure);
    RUN_TEST(test_plugin_empty_releases);
    RUN_TEST(test_plugin_malformed_json);
    RUN_TEST(test_plugin_null_inputs);
    RUN_TEST(test_plugin_zero_count);
    RUN_TEST(test_plugin_missing_asset);

    return UNITY_END();
}
