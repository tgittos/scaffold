/**
 * Unit tests for rate_limiter module
 */

#include "unity/unity.h"
#include "../src/policy/rate_limiter.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

/* =============================================================================
 * Lifecycle Tests
 * ========================================================================== */

void test_rate_limiter_create_returns_valid_limiter(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);
    rate_limiter_destroy(rl);
}

void test_rate_limiter_destroy_null_is_safe(void) {
    rate_limiter_destroy(NULL);
    /* Should not crash */
    TEST_PASS();
}

/* =============================================================================
 * Blocking Tests
 * ========================================================================== */

void test_rate_limiter_new_key_is_not_blocked(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(rl, "test_tool"));

    rate_limiter_destroy(rl);
}

void test_rate_limiter_is_blocked_null_limiter(void) {
    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(NULL, "test_tool"));
}

void test_rate_limiter_is_blocked_null_key(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(rl, NULL));
    rate_limiter_destroy(rl);
}

/* =============================================================================
 * Denial Recording Tests
 * ========================================================================== */

void test_rate_limiter_first_denial_no_backoff(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    rate_limiter_record_denial(rl, "test_tool");

    /* First denial should not cause blocking (backoff = 0) */
    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(rl, "test_tool"));

    rate_limiter_destroy(rl);
}

void test_rate_limiter_second_denial_no_backoff(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    rate_limiter_record_denial(rl, "test_tool");
    rate_limiter_record_denial(rl, "test_tool");

    /* Second denial should not cause blocking (backoff = 0) */
    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(rl, "test_tool"));

    rate_limiter_destroy(rl);
}

void test_rate_limiter_third_denial_causes_backoff(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    rate_limiter_record_denial(rl, "test_tool");
    rate_limiter_record_denial(rl, "test_tool");
    rate_limiter_record_denial(rl, "test_tool");

    /* Third denial should cause 5 second backoff */
    TEST_ASSERT_EQUAL(1, rate_limiter_is_blocked(rl, "test_tool"));
    TEST_ASSERT_GREATER_THAN(0, rate_limiter_get_remaining(rl, "test_tool"));

    rate_limiter_destroy(rl);
}

void test_rate_limiter_record_denial_null_limiter(void) {
    /* Should not crash */
    rate_limiter_record_denial(NULL, "test_tool");
    TEST_PASS();
}

void test_rate_limiter_record_denial_null_key(void) {
    RateLimiter *rl = rate_limiter_create();
    /* Should not crash */
    rate_limiter_record_denial(rl, NULL);
    rate_limiter_destroy(rl);
    TEST_PASS();
}

/* =============================================================================
 * Reset Tests
 * ========================================================================== */

void test_rate_limiter_reset_clears_blocking(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    /* Record enough denials to trigger blocking */
    for (int i = 0; i < 5; i++) {
        rate_limiter_record_denial(rl, "test_tool");
    }
    TEST_ASSERT_EQUAL(1, rate_limiter_is_blocked(rl, "test_tool"));

    /* Reset should clear blocking */
    rate_limiter_reset(rl, "test_tool");
    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(rl, "test_tool"));
    TEST_ASSERT_EQUAL(0, rate_limiter_get_remaining(rl, "test_tool"));

    rate_limiter_destroy(rl);
}

void test_rate_limiter_reset_nonexistent_key(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    /* Should not crash */
    rate_limiter_reset(rl, "nonexistent");

    rate_limiter_destroy(rl);
}

void test_rate_limiter_reset_null_limiter(void) {
    /* Should not crash */
    rate_limiter_reset(NULL, "test_tool");
    TEST_PASS();
}

void test_rate_limiter_reset_null_key(void) {
    RateLimiter *rl = rate_limiter_create();
    /* Should not crash */
    rate_limiter_reset(rl, NULL);
    rate_limiter_destroy(rl);
    TEST_PASS();
}

/* =============================================================================
 * Get Remaining Tests
 * ========================================================================== */

void test_rate_limiter_get_remaining_no_denial(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    TEST_ASSERT_EQUAL(0, rate_limiter_get_remaining(rl, "test_tool"));

    rate_limiter_destroy(rl);
}

void test_rate_limiter_get_remaining_null_limiter(void) {
    TEST_ASSERT_EQUAL(0, rate_limiter_get_remaining(NULL, "test_tool"));
}

void test_rate_limiter_get_remaining_null_key(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_EQUAL(0, rate_limiter_get_remaining(rl, NULL));
    rate_limiter_destroy(rl);
}

/* =============================================================================
 * Multiple Keys Tests
 * ========================================================================== */

void test_rate_limiter_multiple_keys_independent(void) {
    RateLimiter *rl = rate_limiter_create();
    TEST_ASSERT_NOT_NULL(rl);

    /* Record denials for tool_a */
    for (int i = 0; i < 5; i++) {
        rate_limiter_record_denial(rl, "tool_a");
    }

    /* tool_a should be blocked, tool_b should not */
    TEST_ASSERT_EQUAL(1, rate_limiter_is_blocked(rl, "tool_a"));
    TEST_ASSERT_EQUAL(0, rate_limiter_is_blocked(rl, "tool_b"));

    rate_limiter_destroy(rl);
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Lifecycle tests */
    RUN_TEST(test_rate_limiter_create_returns_valid_limiter);
    RUN_TEST(test_rate_limiter_destroy_null_is_safe);

    /* Blocking tests */
    RUN_TEST(test_rate_limiter_new_key_is_not_blocked);
    RUN_TEST(test_rate_limiter_is_blocked_null_limiter);
    RUN_TEST(test_rate_limiter_is_blocked_null_key);

    /* Denial recording tests */
    RUN_TEST(test_rate_limiter_first_denial_no_backoff);
    RUN_TEST(test_rate_limiter_second_denial_no_backoff);
    RUN_TEST(test_rate_limiter_third_denial_causes_backoff);
    RUN_TEST(test_rate_limiter_record_denial_null_limiter);
    RUN_TEST(test_rate_limiter_record_denial_null_key);

    /* Reset tests */
    RUN_TEST(test_rate_limiter_reset_clears_blocking);
    RUN_TEST(test_rate_limiter_reset_nonexistent_key);
    RUN_TEST(test_rate_limiter_reset_null_limiter);
    RUN_TEST(test_rate_limiter_reset_null_key);

    /* Get remaining tests */
    RUN_TEST(test_rate_limiter_get_remaining_no_denial);
    RUN_TEST(test_rate_limiter_get_remaining_null_limiter);
    RUN_TEST(test_rate_limiter_get_remaining_null_key);

    /* Multiple keys tests */
    RUN_TEST(test_rate_limiter_multiple_keys_independent);

    return UNITY_END();
}
