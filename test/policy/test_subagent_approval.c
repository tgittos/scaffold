/**
 * Unit Tests for Subagent Approval Proxy
 *
 * Tests the IPC-based approval proxying for subagents.
 *
 * NOTE: This test file is excluded from valgrind per CLAUDE.md guidelines.
 * Subagent tests use fork() which can cause issues under valgrind.
 * For memory safety testing, test the serialization/deserialization functions
 * directly without the fork/exec layer.
 */

#include "unity.h"
#include "approval_gate.h"
#include "subagent_approval.h"

#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ralph_home.h"

/* =============================================================================
 * Stubs for Python tool functions (not linked in unit tests)
 * ========================================================================== */

int is_python_file_tool(const char *name) {
    (void)name;
    return 0;  /* In unit tests, no Python tools are loaded */
}

const char* python_tool_get_gate_category(const char *name) {
    (void)name;
    return NULL;
}

const char* python_tool_get_match_arg(const char *name) {
    (void)name;
    return NULL;
}

void setUp(void) {
    ralph_home_init(NULL);
}

void tearDown(void) {

    ralph_home_cleanup();
}

/* =============================================================================
 * Pipe Creation Tests
 * ========================================================================== */

static void test_create_approval_channel_pipes_creates_valid_pipes(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};

    int result = create_approval_channel_pipes(request_pipe, response_pipe);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_GREATER_OR_EQUAL(0, request_pipe[0]);
    TEST_ASSERT_GREATER_OR_EQUAL(0, request_pipe[1]);
    TEST_ASSERT_GREATER_OR_EQUAL(0, response_pipe[0]);
    TEST_ASSERT_GREATER_OR_EQUAL(0, response_pipe[1]);

    /* Clean up */
    close(request_pipe[0]);
    close(request_pipe[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
}

static void test_create_approval_channel_pipes_null_request_returns_error(void) {
    int response_pipe[2] = {-1, -1};

    int result = create_approval_channel_pipes(NULL, response_pipe);

    TEST_ASSERT_EQUAL_INT(-1, result);
}

static void test_create_approval_channel_pipes_null_response_returns_error(void) {
    int request_pipe[2] = {-1, -1};

    int result = create_approval_channel_pipes(request_pipe, NULL);

    TEST_ASSERT_EQUAL_INT(-1, result);
}

/* =============================================================================
 * Channel Setup Tests
 * ========================================================================== */

static void test_setup_subagent_channel_child_closes_parent_ends(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};
    ApprovalChannel channel = {0};

    create_approval_channel_pipes(request_pipe, response_pipe);

    /* Setup child channel */
    setup_subagent_channel_child(&channel, request_pipe, response_pipe);

    /* Child should have write end of request, read end of response */
    TEST_ASSERT_EQUAL_INT(request_pipe[1], channel.request_fd);
    TEST_ASSERT_EQUAL_INT(response_pipe[0], channel.response_fd);
    TEST_ASSERT_GREATER_THAN(0, channel.subagent_pid);

    /* Parent ends should be closed (attempting to write should fail or succeed
     * depending on timing - we just check our fds are valid) */
    TEST_ASSERT_GREATER_OR_EQUAL(0, channel.request_fd);
    TEST_ASSERT_GREATER_OR_EQUAL(0, channel.response_fd);

    /* Clean up */
    close(channel.request_fd);
    close(channel.response_fd);
}

static void test_setup_subagent_channel_parent_closes_child_ends(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};
    ApprovalChannel channel = {0};

    create_approval_channel_pipes(request_pipe, response_pipe);

    /* Setup parent channel */
    pid_t fake_pid = 12345;
    setup_subagent_channel_parent(&channel, request_pipe, response_pipe, fake_pid);

    /* Parent should have read end of request, write end of response */
    TEST_ASSERT_EQUAL_INT(request_pipe[0], channel.request_fd);
    TEST_ASSERT_EQUAL_INT(response_pipe[1], channel.response_fd);
    TEST_ASSERT_EQUAL_INT(fake_pid, channel.subagent_pid);

    /* Clean up */
    close(channel.request_fd);
    close(channel.response_fd);
}

static void test_setup_subagent_channel_child_handles_null(void) {
    int request_pipe[2] = {0, 0};
    int response_pipe[2] = {0, 0};

    /* Should not crash */
    setup_subagent_channel_child(NULL, request_pipe, response_pipe);
}

static void test_setup_subagent_channel_parent_handles_null(void) {
    int request_pipe[2] = {0, 0};
    int response_pipe[2] = {0, 0};

    /* Should not crash */
    setup_subagent_channel_parent(NULL, request_pipe, response_pipe, 12345);
}

/* =============================================================================
 * Cleanup Tests
 * ========================================================================== */

static void test_cleanup_approval_channel_pipes_closes_all(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};

    create_approval_channel_pipes(request_pipe, response_pipe);

    /* Verify pipes are valid */
    TEST_ASSERT_GREATER_OR_EQUAL(0, request_pipe[0]);
    TEST_ASSERT_GREATER_OR_EQUAL(0, request_pipe[1]);
    TEST_ASSERT_GREATER_OR_EQUAL(0, response_pipe[0]);
    TEST_ASSERT_GREATER_OR_EQUAL(0, response_pipe[1]);

    /* Cleanup */
    cleanup_approval_channel_pipes(request_pipe, response_pipe);

    /* Pipes should be closed - trying to use them should fail */
    /* We can't easily verify closure without causing side effects,
     * so just verify the function doesn't crash */
}

static void test_cleanup_approval_channel_pipes_handles_null(void) {
    /* Should not crash */
    cleanup_approval_channel_pipes(NULL, NULL);
}

static void test_free_approval_channel_handles_null(void) {
    /* Should not crash */
    free_approval_channel(NULL);
}

static void test_free_approval_channel_closes_fds(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};
    ApprovalChannel *channel = malloc(sizeof(ApprovalChannel));

    TEST_ASSERT_NOT_NULL(channel);

    create_approval_channel_pipes(request_pipe, response_pipe);
    channel->request_fd = request_pipe[0];
    channel->response_fd = response_pipe[1];
    channel->subagent_pid = 12345;

    /* Close other ends so free_approval_channel doesn't fail */
    close(request_pipe[1]);
    close(response_pipe[0]);

    /* This should close the fds and free the struct */
    free_approval_channel(channel);

    /* Can't verify closure easily, but verify no crash */
}

/* =============================================================================
 * Poll Tests
 * ========================================================================== */

static void test_poll_subagent_approval_requests_no_data_returns_negative(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};

    create_approval_channel_pipes(request_pipe, response_pipe);

    ApprovalChannel channel = {
        .request_fd = request_pipe[0],
        .response_fd = response_pipe[1],
        .subagent_pid = 12345
    };

    /* Poll with very short timeout - should return -1 (no data) */
    int result = poll_subagent_approval_requests(&channel, 1, 1);

    TEST_ASSERT_EQUAL_INT(-1, result);

    /* Clean up */
    close(request_pipe[0]);
    close(request_pipe[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
}

static void test_poll_subagent_approval_requests_null_returns_negative(void) {
    int result = poll_subagent_approval_requests(NULL, 1, 100);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

static void test_poll_subagent_approval_requests_zero_count_returns_negative(void) {
    ApprovalChannel channel = {0};
    int result = poll_subagent_approval_requests(&channel, 0, 100);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

/* =============================================================================
 * Subagent Request Approval Tests
 * ========================================================================== */

static void test_subagent_request_approval_null_channel_returns_denied(void) {
    ToolCall tool_call = {
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}",
        .id = "test-1"
    };

    ApprovalResult result = subagent_request_approval(NULL, &tool_call, NULL);
    TEST_ASSERT_EQUAL_INT(APPROVAL_DENIED, result);
}

static void test_subagent_request_approval_null_tool_call_returns_denied(void) {
    ApprovalChannel channel = {
        .request_fd = 1,
        .response_fd = 0,
        .subagent_pid = 12345
    };

    ApprovalResult result = subagent_request_approval(&channel, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(APPROVAL_DENIED, result);
}

/* =============================================================================
 * Handle Request Tests
 * ========================================================================== */

static void test_handle_subagent_approval_request_null_config_safe(void) {
    ApprovalChannel channel = {0};

    /* Should not crash */
    handle_subagent_approval_request(NULL, &channel, NULL);
}

static void test_handle_subagent_approval_request_null_channel_safe(void) {
    ApprovalGateConfig config = {0};

    /* Should not crash */
    handle_subagent_approval_request(&config, NULL, NULL);
}

/* =============================================================================
 * End-to-End Pipe Communication Test (no fork)
 * ========================================================================== */

static void test_pipe_communication_write_and_read(void) {
    int request_pipe[2] = {-1, -1};
    int response_pipe[2] = {-1, -1};

    create_approval_channel_pipes(request_pipe, response_pipe);

    /* Write a test message */
    const char *test_msg = "test message";
    ssize_t written = write(request_pipe[1], test_msg, strlen(test_msg) + 1);
    TEST_ASSERT_GREATER_THAN(0, written);

    /* Read it back */
    char buffer[256];
    ssize_t read_bytes = read(request_pipe[0], buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(0, read_bytes);
    TEST_ASSERT_EQUAL_STRING(test_msg, buffer);

    /* Clean up */
    close(request_pipe[0]);
    close(request_pipe[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
}

/* =============================================================================
 * Test Runner
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Pipe Creation Tests */
    RUN_TEST(test_create_approval_channel_pipes_creates_valid_pipes);
    RUN_TEST(test_create_approval_channel_pipes_null_request_returns_error);
    RUN_TEST(test_create_approval_channel_pipes_null_response_returns_error);

    /* Channel Setup Tests */
    RUN_TEST(test_setup_subagent_channel_child_closes_parent_ends);
    RUN_TEST(test_setup_subagent_channel_parent_closes_child_ends);
    RUN_TEST(test_setup_subagent_channel_child_handles_null);
    RUN_TEST(test_setup_subagent_channel_parent_handles_null);

    /* Cleanup Tests */
    RUN_TEST(test_cleanup_approval_channel_pipes_closes_all);
    RUN_TEST(test_cleanup_approval_channel_pipes_handles_null);
    RUN_TEST(test_free_approval_channel_handles_null);
    RUN_TEST(test_free_approval_channel_closes_fds);

    /* Poll Tests */
    RUN_TEST(test_poll_subagent_approval_requests_no_data_returns_negative);
    RUN_TEST(test_poll_subagent_approval_requests_null_returns_negative);
    RUN_TEST(test_poll_subagent_approval_requests_zero_count_returns_negative);

    /* Subagent Request Tests */
    RUN_TEST(test_subagent_request_approval_null_channel_returns_denied);
    RUN_TEST(test_subagent_request_approval_null_tool_call_returns_denied);

    /* Handle Request Tests */
    RUN_TEST(test_handle_subagent_approval_request_null_config_safe);
    RUN_TEST(test_handle_subagent_approval_request_null_channel_safe);

    /* End-to-End Tests */
    RUN_TEST(test_pipe_communication_write_and_read);

    return UNITY_END();
}
