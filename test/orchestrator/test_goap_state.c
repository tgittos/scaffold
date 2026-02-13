#include "unity.h"
#include "orchestrator/goap_state.h"

void setUp(void) {}
void tearDown(void) {}

void test_preconditions_met_null(void) {
    TEST_ASSERT_TRUE(goap_preconditions_met(NULL, "{}"));
}

void test_preconditions_met_empty_array(void) {
    TEST_ASSERT_TRUE(goap_preconditions_met("[]", "{}"));
}

void test_preconditions_met_satisfied(void) {
    TEST_ASSERT_TRUE(goap_preconditions_met(
        "[\"a\",\"b\"]",
        "{\"a\":true,\"b\":true,\"c\":false}"));
}

void test_preconditions_met_unsatisfied(void) {
    TEST_ASSERT_FALSE(goap_preconditions_met(
        "[\"a\",\"b\"]",
        "{\"a\":true,\"b\":false}"));
}

void test_preconditions_met_missing_key(void) {
    TEST_ASSERT_FALSE(goap_preconditions_met(
        "[\"a\",\"b\"]",
        "{\"a\":true}"));
}

void test_preconditions_met_null_world_state(void) {
    TEST_ASSERT_FALSE(goap_preconditions_met("[\"a\"]", NULL));
}

void test_preconditions_met_malformed_json(void) {
    TEST_ASSERT_TRUE(goap_preconditions_met("{not valid}", "{}"));
}

void test_preconditions_met_empty_world_state(void) {
    TEST_ASSERT_FALSE(goap_preconditions_met("[\"a\"]", "{}"));
}

void test_check_progress_null_goal_state(void) {
    GoapProgress p = goap_check_progress(NULL, "{\"a\":true}");
    TEST_ASSERT_FALSE(p.complete);
    TEST_ASSERT_EQUAL(0, p.satisfied);
    TEST_ASSERT_EQUAL(0, p.total);
}

void test_check_progress_empty_goal_state(void) {
    GoapProgress p = goap_check_progress("{}", "{}");
    TEST_ASSERT_FALSE(p.complete);
    TEST_ASSERT_EQUAL(0, p.total);
}

void test_check_progress_all_satisfied(void) {
    GoapProgress p = goap_check_progress(
        "{\"a\":true,\"b\":true}",
        "{\"a\":true,\"b\":true,\"c\":true}");
    TEST_ASSERT_TRUE(p.complete);
    TEST_ASSERT_EQUAL(2, p.satisfied);
    TEST_ASSERT_EQUAL(2, p.total);
}

void test_check_progress_partial(void) {
    GoapProgress p = goap_check_progress(
        "{\"a\":true,\"b\":true,\"c\":true}",
        "{\"a\":true,\"b\":false}");
    TEST_ASSERT_FALSE(p.complete);
    TEST_ASSERT_EQUAL(1, p.satisfied);
    TEST_ASSERT_EQUAL(3, p.total);
}

void test_check_progress_null_world_state(void) {
    GoapProgress p = goap_check_progress("{\"a\":true}", NULL);
    TEST_ASSERT_FALSE(p.complete);
    TEST_ASSERT_EQUAL(0, p.satisfied);
    TEST_ASSERT_EQUAL(1, p.total);
}

void test_check_progress_malformed_goal_state(void) {
    GoapProgress p = goap_check_progress("not json", "{\"a\":true}");
    TEST_ASSERT_FALSE(p.complete);
    TEST_ASSERT_EQUAL(0, p.total);
}

void test_check_progress_single_assertion(void) {
    GoapProgress p = goap_check_progress("{\"done\":true}", "{\"done\":true}");
    TEST_ASSERT_TRUE(p.complete);
    TEST_ASSERT_EQUAL(1, p.satisfied);
    TEST_ASSERT_EQUAL(1, p.total);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_preconditions_met_null);
    RUN_TEST(test_preconditions_met_empty_array);
    RUN_TEST(test_preconditions_met_satisfied);
    RUN_TEST(test_preconditions_met_unsatisfied);
    RUN_TEST(test_preconditions_met_missing_key);
    RUN_TEST(test_preconditions_met_null_world_state);
    RUN_TEST(test_preconditions_met_malformed_json);
    RUN_TEST(test_preconditions_met_empty_world_state);
    RUN_TEST(test_check_progress_null_goal_state);
    RUN_TEST(test_check_progress_empty_goal_state);
    RUN_TEST(test_check_progress_all_satisfied);
    RUN_TEST(test_check_progress_partial);
    RUN_TEST(test_check_progress_null_world_state);
    RUN_TEST(test_check_progress_malformed_goal_state);
    RUN_TEST(test_check_progress_single_assertion);

    return UNITY_END();
}
