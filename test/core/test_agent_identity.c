#include "unity/unity.h"
#include "ipc/agent_identity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_create_with_id_only(void) {
    AgentIdentity* identity = agent_identity_create("test-agent-123", NULL);
    TEST_ASSERT_NOT_NULL(identity);

    char* id = agent_identity_get_id(identity);
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_EQUAL_STRING("test-agent-123", id);
    free(id);

    char* parent = agent_identity_get_parent_id(identity);
    TEST_ASSERT_NULL(parent);

    TEST_ASSERT_FALSE(agent_identity_is_subagent(identity));

    agent_identity_destroy(identity);
}

void test_create_with_parent_id(void) {
    AgentIdentity* identity = agent_identity_create("child-agent", "parent-agent");
    TEST_ASSERT_NOT_NULL(identity);

    char* id = agent_identity_get_id(identity);
    TEST_ASSERT_EQUAL_STRING("child-agent", id);
    free(id);

    char* parent = agent_identity_get_parent_id(identity);
    TEST_ASSERT_NOT_NULL(parent);
    TEST_ASSERT_EQUAL_STRING("parent-agent", parent);
    free(parent);

    TEST_ASSERT_TRUE(agent_identity_is_subagent(identity));

    agent_identity_destroy(identity);
}

void test_create_with_empty_parent_id(void) {
    AgentIdentity* identity = agent_identity_create("agent", "");
    TEST_ASSERT_NOT_NULL(identity);

    TEST_ASSERT_FALSE(agent_identity_is_subagent(identity));

    char* parent = agent_identity_get_parent_id(identity);
    TEST_ASSERT_NULL(parent);

    agent_identity_destroy(identity);
}

void test_create_null_id_allowed(void) {
    AgentIdentity* identity = agent_identity_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(identity);

    char* id = agent_identity_get_id(identity);
    TEST_ASSERT_NULL(id);

    agent_identity_destroy(identity);
}

void test_destroy_null_is_safe(void) {
    agent_identity_destroy(NULL);
    TEST_PASS();
}

void test_get_id_returns_copy(void) {
    AgentIdentity* identity = agent_identity_create("original", NULL);

    char* id1 = agent_identity_get_id(identity);
    char* id2 = agent_identity_get_id(identity);

    TEST_ASSERT_NOT_NULL(id1);
    TEST_ASSERT_NOT_NULL(id2);
    TEST_ASSERT_NOT_EQUAL(id1, id2);  /* Different pointers */
    TEST_ASSERT_EQUAL_STRING(id1, id2);  /* Same content */

    free(id1);
    free(id2);
    agent_identity_destroy(identity);
}

void test_get_id_null_identity(void) {
    TEST_ASSERT_NULL(agent_identity_get_id(NULL));
}

void test_get_parent_id_null_identity(void) {
    TEST_ASSERT_NULL(agent_identity_get_parent_id(NULL));
}

void test_is_subagent_null_identity(void) {
    TEST_ASSERT_FALSE(agent_identity_is_subagent(NULL));
}

void test_set_id(void) {
    AgentIdentity* identity = agent_identity_create("old-id", NULL);

    TEST_ASSERT_EQUAL_INT(0, agent_identity_set_id(identity, "new-id"));

    char* id = agent_identity_get_id(identity);
    TEST_ASSERT_EQUAL_STRING("new-id", id);
    free(id);

    agent_identity_destroy(identity);
}

void test_set_id_null_clears(void) {
    AgentIdentity* identity = agent_identity_create("some-id", NULL);

    TEST_ASSERT_EQUAL_INT(0, agent_identity_set_id(identity, NULL));

    char* id = agent_identity_get_id(identity);
    TEST_ASSERT_NULL(id);

    agent_identity_destroy(identity);
}

void test_set_id_null_identity(void) {
    TEST_ASSERT_EQUAL_INT(-1, agent_identity_set_id(NULL, "test"));
}

void test_set_parent_id(void) {
    AgentIdentity* identity = agent_identity_create("agent", NULL);

    TEST_ASSERT_FALSE(agent_identity_is_subagent(identity));

    TEST_ASSERT_EQUAL_INT(0, agent_identity_set_parent_id(identity, "new-parent"));

    char* parent = agent_identity_get_parent_id(identity);
    TEST_ASSERT_EQUAL_STRING("new-parent", parent);
    free(parent);

    TEST_ASSERT_TRUE(agent_identity_is_subagent(identity));

    agent_identity_destroy(identity);
}

void test_set_parent_id_null_clears(void) {
    AgentIdentity* identity = agent_identity_create("agent", "parent");

    TEST_ASSERT_TRUE(agent_identity_is_subagent(identity));

    TEST_ASSERT_EQUAL_INT(0, agent_identity_set_parent_id(identity, NULL));

    char* parent = agent_identity_get_parent_id(identity);
    TEST_ASSERT_NULL(parent);

    TEST_ASSERT_FALSE(agent_identity_is_subagent(identity));

    agent_identity_destroy(identity);
}

void test_set_parent_id_null_identity(void) {
    TEST_ASSERT_EQUAL_INT(-1, agent_identity_set_parent_id(NULL, "test"));
}

void test_id_truncation_at_max_length(void) {
    /* Create a long string */
    char long_id[100];
    memset(long_id, 'a', 99);
    long_id[99] = '\0';

    AgentIdentity* identity = agent_identity_create(long_id, NULL);
    TEST_ASSERT_NOT_NULL(identity);

    char* id = agent_identity_get_id(identity);
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_EQUAL_INT(AGENT_ID_MAX_LENGTH - 1, strlen(id));

    free(id);
    agent_identity_destroy(identity);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_with_id_only);
    RUN_TEST(test_create_with_parent_id);
    RUN_TEST(test_create_with_empty_parent_id);
    RUN_TEST(test_create_null_id_allowed);
    RUN_TEST(test_destroy_null_is_safe);
    RUN_TEST(test_get_id_returns_copy);
    RUN_TEST(test_get_id_null_identity);
    RUN_TEST(test_get_parent_id_null_identity);
    RUN_TEST(test_is_subagent_null_identity);
    RUN_TEST(test_set_id);
    RUN_TEST(test_set_id_null_clears);
    RUN_TEST(test_set_id_null_identity);
    RUN_TEST(test_set_parent_id);
    RUN_TEST(test_set_parent_id_null_clears);
    RUN_TEST(test_set_parent_id_null_identity);
    RUN_TEST(test_id_truncation_at_max_length);

    return UNITY_END();
}
