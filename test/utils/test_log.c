#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../unity/unity.h"
#include "util/log.h"

void setUp(void) {
}

void tearDown(void) {
    /* Reset to disabled after each test */
    log_init(LOG_ERROR, 0);
}

void test_log_enabled_respects_level(void) {
    log_init(LOG_INFO, LOG_MOD_ALL);

    TEST_ASSERT_TRUE(log_enabled(LOG_ERROR, LOG_MOD_AGENT));
    TEST_ASSERT_TRUE(log_enabled(LOG_WARN, LOG_MOD_AGENT));
    TEST_ASSERT_TRUE(log_enabled(LOG_INFO, LOG_MOD_AGENT));
    TEST_ASSERT_FALSE(log_enabled(LOG_DEBUG, LOG_MOD_AGENT));
}

void test_log_enabled_respects_module(void) {
    log_init(LOG_DEBUG, LOG_MOD_TOOL | LOG_MOD_GOAP);

    TEST_ASSERT_TRUE(log_enabled(LOG_INFO, LOG_MOD_TOOL));
    TEST_ASSERT_TRUE(log_enabled(LOG_INFO, LOG_MOD_GOAP));
    TEST_ASSERT_FALSE(log_enabled(LOG_INFO, LOG_MOD_HTTP));
    TEST_ASSERT_FALSE(log_enabled(LOG_INFO, LOG_MOD_AGENT));
}

void test_log_enabled_disabled_by_default(void) {
    log_init(LOG_ERROR, 0);

    TEST_ASSERT_FALSE(log_enabled(LOG_ERROR, LOG_MOD_AGENT));
    TEST_ASSERT_FALSE(log_enabled(LOG_DEBUG, LOG_MOD_ALL));
}

void test_log_enabled_all_modules(void) {
    log_init(LOG_DEBUG, LOG_MOD_ALL);

    TEST_ASSERT_TRUE(log_enabled(LOG_DEBUG, LOG_MOD_AGENT));
    TEST_ASSERT_TRUE(log_enabled(LOG_DEBUG, LOG_MOD_TOOL));
    TEST_ASSERT_TRUE(log_enabled(LOG_DEBUG, LOG_MOD_HTTP));
    TEST_ASSERT_TRUE(log_enabled(LOG_DEBUG, LOG_MOD_MCP));
    TEST_ASSERT_TRUE(log_enabled(LOG_DEBUG, LOG_MOD_PLUGIN));
}

void test_log_parse_level_valid(void) {
    TEST_ASSERT_EQUAL_INT(LOG_ERROR, log_parse_level("error"));
    TEST_ASSERT_EQUAL_INT(LOG_WARN, log_parse_level("warn"));
    TEST_ASSERT_EQUAL_INT(LOG_INFO, log_parse_level("info"));
    TEST_ASSERT_EQUAL_INT(LOG_DEBUG, log_parse_level("debug"));
}

void test_log_parse_level_case_insensitive(void) {
    TEST_ASSERT_EQUAL_INT(LOG_ERROR, log_parse_level("ERROR"));
    TEST_ASSERT_EQUAL_INT(LOG_INFO, log_parse_level("Info"));
    TEST_ASSERT_EQUAL_INT(LOG_DEBUG, log_parse_level("DEBUG"));
}

void test_log_parse_level_invalid(void) {
    TEST_ASSERT_EQUAL_INT(-1, log_parse_level(NULL));
    TEST_ASSERT_EQUAL_INT(-1, log_parse_level(""));
    TEST_ASSERT_EQUAL_INT(-1, log_parse_level("verbose"));
    TEST_ASSERT_EQUAL_INT(-1, log_parse_level("trace"));
}

void test_log_parse_modules_single(void) {
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_TOOL, log_parse_modules("tool"));
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_HTTP, log_parse_modules("http"));
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_GOAP, log_parse_modules("goap"));
}

void test_log_parse_modules_multiple(void) {
    uint32_t mask = log_parse_modules("tool,goap,http");
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_TOOL | LOG_MOD_GOAP | LOG_MOD_HTTP, mask);
}

void test_log_parse_modules_all(void) {
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_ALL, log_parse_modules("all"));
}

void test_log_parse_modules_case_insensitive(void) {
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_TOOL, log_parse_modules("TOOL"));
    uint32_t mask = log_parse_modules("Tool,GOAP");
    TEST_ASSERT_EQUAL_UINT32(LOG_MOD_TOOL | LOG_MOD_GOAP, mask);
}

void test_log_parse_modules_null(void) {
    TEST_ASSERT_EQUAL_UINT32(0, log_parse_modules(NULL));
}

void test_log_http_body_default(void) {
    TEST_ASSERT_FALSE(log_http_body_enabled);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_log_enabled_respects_level);
    RUN_TEST(test_log_enabled_respects_module);
    RUN_TEST(test_log_enabled_disabled_by_default);
    RUN_TEST(test_log_enabled_all_modules);
    RUN_TEST(test_log_parse_level_valid);
    RUN_TEST(test_log_parse_level_case_insensitive);
    RUN_TEST(test_log_parse_level_invalid);
    RUN_TEST(test_log_parse_modules_single);
    RUN_TEST(test_log_parse_modules_multiple);
    RUN_TEST(test_log_parse_modules_all);
    RUN_TEST(test_log_parse_modules_case_insensitive);
    RUN_TEST(test_log_parse_modules_null);
    RUN_TEST(test_log_http_body_default);

    return UNITY_END();
}
