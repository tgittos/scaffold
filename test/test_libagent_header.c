/*
 * test_libagent_header.c - Verify the flat header compiles and libagent.a links
 *
 * This test includes ONLY the generated out/libagent.h (not individual lib/
 * headers) and calls functions from multiple modules. If it compiles and
 * links, the flat header + static library are distributable.
 */

#include "libagent.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_flat_header_compiles(void) {
    /* If we got here, the flat header compiled without errors —
       no missing types, no duplicate definitions, no include ordering issues. */
    TEST_PASS();
}

void test_tool_registry_lifecycle(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    TEST_ASSERT_EQUAL(0, registry.functions.count);
    cleanup_tool_registry(&registry);
}

void test_services_lifecycle(void) {
    Services *svc = services_create_empty();
    TEST_ASSERT_NOT_NULL(svc);
    services_destroy(svc);
}

void test_pipe_notifier_lifecycle(void) {
    PipeNotifier pn;
    int rc = pipe_notifier_init(&pn);
    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_TRUE(pipe_notifier_get_read_fd(&pn) >= 0);
    pipe_notifier_destroy(&pn);
}

void test_agent_identity_lifecycle(void) {
    AgentIdentity *id = agent_identity_create("test-id", NULL);
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_EQUAL_STRING("test-id", agent_identity_get_id(id));
    TEST_ASSERT_FALSE(agent_identity_is_subagent(id));
    agent_identity_destroy(id);
}

void test_version_defined(void) {
    TEST_ASSERT_NOT_NULL(RALPH_VERSION);
    TEST_ASSERT_TRUE(strlen(RALPH_VERSION) > 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_flat_header_compiles);
    RUN_TEST(test_tool_registry_lifecycle);
    RUN_TEST(test_services_lifecycle);
    RUN_TEST(test_pipe_notifier_lifecycle);
    RUN_TEST(test_agent_identity_lifecycle);
    RUN_TEST(test_version_defined);
    return UNITY_END();
}
