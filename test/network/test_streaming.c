#include "unity/unity.h"
#include "streaming.h"
#include <stdlib.h>
#include <string.h>

// Test callback state
static char* callback_text_buffer = NULL;
static size_t callback_text_len = 0;
static char* callback_thinking_buffer = NULL;
static size_t callback_thinking_len = 0;
static int tool_start_count = 0;
static int tool_delta_count = 0;
static int stream_end_count = 0;
static int error_count = 0;
static char* last_stop_reason = NULL;
static char* last_error_message = NULL;

void setUp(void)
{
    // Reset callback state before each test
    free(callback_text_buffer);
    callback_text_buffer = NULL;
    callback_text_len = 0;

    free(callback_thinking_buffer);
    callback_thinking_buffer = NULL;
    callback_thinking_len = 0;

    tool_start_count = 0;
    tool_delta_count = 0;
    stream_end_count = 0;
    error_count = 0;

    free(last_stop_reason);
    last_stop_reason = NULL;

    free(last_error_message);
    last_error_message = NULL;
}

void tearDown(void)
{
    // Clean up after each test
    free(callback_text_buffer);
    callback_text_buffer = NULL;
    free(callback_thinking_buffer);
    callback_thinking_buffer = NULL;
    free(last_stop_reason);
    last_stop_reason = NULL;
    free(last_error_message);
    last_error_message = NULL;
}

// =============================================================================
// Test Callbacks
// =============================================================================

static void test_text_callback(const char* text, size_t len, void* user_data)
{
    (void)user_data;
    char* new_buffer = realloc(callback_text_buffer, callback_text_len + len + 1);
    if (new_buffer) {
        callback_text_buffer = new_buffer;
        memcpy(callback_text_buffer + callback_text_len, text, len);
        callback_text_len += len;
        callback_text_buffer[callback_text_len] = '\0';
    }
}

static void test_thinking_callback(const char* text, size_t len, void* user_data)
{
    (void)user_data;
    char* new_buffer = realloc(callback_thinking_buffer, callback_thinking_len + len + 1);
    if (new_buffer) {
        callback_thinking_buffer = new_buffer;
        memcpy(callback_thinking_buffer + callback_thinking_len, text, len);
        callback_thinking_len += len;
        callback_thinking_buffer[callback_thinking_len] = '\0';
    }
}

static void test_tool_start_callback(const char* id, const char* name, void* user_data)
{
    (void)id;
    (void)name;
    (void)user_data;
    tool_start_count++;
}

static void test_tool_delta_callback(const char* id, const char* json_delta, void* user_data)
{
    (void)id;
    (void)json_delta;
    (void)user_data;
    tool_delta_count++;
}

static void test_stream_end_callback(const char* stop_reason, void* user_data)
{
    (void)user_data;
    stream_end_count++;
    if (stop_reason) {
        free(last_stop_reason);
        last_stop_reason = strdup(stop_reason);
    }
}

static void test_error_callback(const char* error, void* user_data)
{
    (void)user_data;
    error_count++;
    if (error) {
        free(last_error_message);
        last_error_message = strdup(error);
    }
}

// =============================================================================
// Context Lifecycle Tests
// =============================================================================

void test_streaming_context_create(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(STREAM_STATE_IDLE, ctx->state);
    TEST_ASSERT_NOT_NULL(ctx->line_buffer);
    TEST_ASSERT_NOT_NULL(ctx->text_content);
    TEST_ASSERT_NOT_NULL(ctx->thinking_content);
    TEST_ASSERT_NOT_NULL(ctx->tool_uses);
    TEST_ASSERT_EQUAL_INT(0, ctx->tool_use_count);
    streaming_context_free(ctx);
}

void test_streaming_context_free_null(void)
{
    // Should not crash with NULL
    streaming_context_free(NULL);
    TEST_ASSERT_TRUE(1);
}

void test_streaming_context_reset(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Add some content
    streaming_emit_text(ctx, "Hello", 5);
    streaming_emit_thinking(ctx, "Thinking", 8);
    streaming_emit_tool_start(ctx, "tool_1", "test_tool");
    ctx->state = STREAM_STATE_READING_DATA;
    ctx->input_tokens = 100;
    ctx->output_tokens = 50;

    // Reset
    streaming_context_reset(ctx);

    // Verify reset state
    TEST_ASSERT_EQUAL(STREAM_STATE_IDLE, ctx->state);
    TEST_ASSERT_EQUAL_STRING("", ctx->text_content);
    TEST_ASSERT_EQUAL_STRING("", ctx->thinking_content);
    TEST_ASSERT_EQUAL_INT(0, ctx->tool_use_count);
    TEST_ASSERT_EQUAL_INT(0, ctx->input_tokens);
    TEST_ASSERT_EQUAL_INT(0, ctx->output_tokens);

    streaming_context_free(ctx);
}

// =============================================================================
// SSE Line Parsing Tests
// =============================================================================

void test_sse_complete_line(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Process a complete data line
    const char* sse_data = "data: {\"type\":\"test\"}\n";
    int result = streaming_process_chunk(ctx, sse_data, strlen(sse_data));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL(STREAM_STATE_READING_DATA, ctx->state);

    streaming_context_free(ctx);
}

void test_sse_partial_lines(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Send partial data
    int result = streaming_process_chunk(ctx, "data: {\"ty", 10);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Line buffer should contain partial data
    TEST_ASSERT_EQUAL_STRING("data: {\"ty", ctx->line_buffer);

    // Complete the line
    result = streaming_process_chunk(ctx, "pe\":\"test\"}\n", 12);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL(STREAM_STATE_READING_DATA, ctx->state);

    streaming_context_free(ctx);
}

void test_sse_done_signal(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_stream_end = test_stream_end_callback;

    // Process [DONE] signal
    const char* done_signal = "data: [DONE]\n";
    int result = streaming_process_chunk(ctx, done_signal, strlen(done_signal));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL(STREAM_STATE_COMPLETE, ctx->state);
    TEST_ASSERT_EQUAL_INT(1, stream_end_count);

    streaming_context_free(ctx);
}

void test_sse_event_line(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Process an event line
    const char* event_line = "event: message_start\n";
    int result = streaming_process_chunk(ctx, event_line, strlen(event_line));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL(STREAM_STATE_READING_EVENT, ctx->state);

    streaming_context_free(ctx);
}

void test_sse_comment_line(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Process a comment line (should be ignored)
    const char* comment_line = ": this is a comment\n";
    int result = streaming_process_chunk(ctx, comment_line, strlen(comment_line));
    TEST_ASSERT_EQUAL_INT(0, result);
    // State should remain IDLE since comments don't change state
    TEST_ASSERT_EQUAL(STREAM_STATE_IDLE, ctx->state);

    streaming_context_free(ctx);
}

void test_sse_empty_line(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Empty lines should not cause issues
    const char* empty_line = "\n";
    int result = streaming_process_chunk(ctx, empty_line, strlen(empty_line));
    TEST_ASSERT_EQUAL_INT(0, result);

    streaming_context_free(ctx);
}

void test_sse_crlf_handling(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Windows-style line endings should work
    const char* crlf_data = "data: {\"test\":1}\r\n";
    int result = streaming_process_chunk(ctx, crlf_data, strlen(crlf_data));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL(STREAM_STATE_READING_DATA, ctx->state);

    streaming_context_free(ctx);
}

void test_sse_multiple_lines_in_chunk(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_stream_end = test_stream_end_callback;

    // Multiple lines in a single chunk
    const char* multi_line = "event: message\ndata: {\"type\":\"text\"}\n\ndata: [DONE]\n";
    int result = streaming_process_chunk(ctx, multi_line, strlen(multi_line));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL(STREAM_STATE_COMPLETE, ctx->state);

    streaming_context_free(ctx);
}

// =============================================================================
// Text Emission Tests
// =============================================================================

void test_emit_text_accumulates(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_text(ctx, "Hello ", 6);
    streaming_emit_text(ctx, "World", 5);
    TEST_ASSERT_EQUAL_STRING("Hello World", ctx->text_content);
    TEST_ASSERT_EQUAL_size_t(11, ctx->text_len);

    streaming_context_free(ctx);
}

void test_emit_text_with_callback(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_text_chunk = test_text_callback;

    streaming_emit_text(ctx, "Test ", 5);
    streaming_emit_text(ctx, "callback", 8);

    TEST_ASSERT_EQUAL_STRING("Test callback", callback_text_buffer);

    streaming_context_free(ctx);
}

void test_emit_text_null_safety(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // These should not crash
    streaming_emit_text(NULL, "test", 4);
    streaming_emit_text(ctx, NULL, 4);
    streaming_emit_text(ctx, "test", 0);

    TEST_ASSERT_EQUAL_STRING("", ctx->text_content);

    streaming_context_free(ctx);
}

void test_emit_text_large_content(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Emit enough content to trigger buffer growth
    for (int i = 0; i < 100; i++) {
        streaming_emit_text(ctx, "This is a test string that should cause buffer growth. ", 55);
    }

    TEST_ASSERT_EQUAL_size_t(5500, ctx->text_len);
    TEST_ASSERT_TRUE(ctx->text_capacity >= 5500);

    streaming_context_free(ctx);
}

// =============================================================================
// Thinking Emission Tests
// =============================================================================

void test_emit_thinking_accumulates(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_thinking(ctx, "Let me ", 7);
    streaming_emit_thinking(ctx, "think", 5);
    TEST_ASSERT_EQUAL_STRING("Let me think", ctx->thinking_content);
    TEST_ASSERT_EQUAL_size_t(12, ctx->thinking_len);

    streaming_context_free(ctx);
}

void test_emit_thinking_with_callback(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_thinking_chunk = test_thinking_callback;

    streaming_emit_thinking(ctx, "Processing ", 11);
    streaming_emit_thinking(ctx, "...", 3);

    TEST_ASSERT_EQUAL_STRING("Processing ...", callback_thinking_buffer);

    streaming_context_free(ctx);
}

// =============================================================================
// Tool Use Tests
// =============================================================================

void test_emit_tool_start(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_tool_start(ctx, "tool_abc123", "shell_command");

    TEST_ASSERT_EQUAL_INT(1, ctx->tool_use_count);
    TEST_ASSERT_EQUAL_STRING("tool_abc123", ctx->tool_uses[0].id);
    TEST_ASSERT_EQUAL_STRING("shell_command", ctx->tool_uses[0].name);
    TEST_ASSERT_EQUAL_INT(0, ctx->current_tool_index);

    streaming_context_free(ctx);
}

void test_emit_tool_start_with_callback(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_tool_use_start = test_tool_start_callback;

    streaming_emit_tool_start(ctx, "tool_1", "test_tool");
    streaming_emit_tool_start(ctx, "tool_2", "another_tool");

    TEST_ASSERT_EQUAL_INT(2, tool_start_count);
    TEST_ASSERT_EQUAL_INT(2, ctx->tool_use_count);

    streaming_context_free(ctx);
}

void test_emit_tool_delta(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_tool_start(ctx, "tool_xyz", "file_read");
    streaming_emit_tool_delta(ctx, "tool_xyz", "{\"path\":", 8);
    streaming_emit_tool_delta(ctx, "tool_xyz", "\"/test\"}", 8);

    TEST_ASSERT_EQUAL_STRING("{\"path\":\"/test\"}", ctx->tool_uses[0].arguments_json);

    streaming_context_free(ctx);
}

void test_emit_tool_delta_with_callback(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_tool_use_delta = test_tool_delta_callback;

    streaming_emit_tool_start(ctx, "tool_1", "test");
    streaming_emit_tool_delta(ctx, "tool_1", "{", 1);
    streaming_emit_tool_delta(ctx, "tool_1", "}", 1);

    TEST_ASSERT_EQUAL_INT(2, tool_delta_count);

    streaming_context_free(ctx);
}

void test_emit_tool_delta_wrong_id(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_tool_start(ctx, "tool_1", "test");
    // Delta with wrong ID should be ignored
    streaming_emit_tool_delta(ctx, "wrong_id", "{\"data\":1}", 10);

    TEST_ASSERT_EQUAL_STRING("", ctx->tool_uses[0].arguments_json);

    streaming_context_free(ctx);
}

void test_multiple_tools(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Add multiple tools
    streaming_emit_tool_start(ctx, "tool_1", "shell");
    streaming_emit_tool_delta(ctx, "tool_1", "{\"cmd\":\"ls\"}", 12);

    streaming_emit_tool_start(ctx, "tool_2", "file_read");
    streaming_emit_tool_delta(ctx, "tool_2", "{\"path\":\"/\"}", 12);

    TEST_ASSERT_EQUAL_INT(2, ctx->tool_use_count);
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"ls\"}", ctx->tool_uses[0].arguments_json);
    TEST_ASSERT_EQUAL_STRING("{\"path\":\"/\"}", ctx->tool_uses[1].arguments_json);

    streaming_context_free(ctx);
}

void test_tool_capacity_growth(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Add many tools to trigger capacity growth
    for (int i = 0; i < 10; i++) {
        char id[32] = {0};
        char name[32] = {0};
        snprintf(id, sizeof(id), "tool_%d", i);
        snprintf(name, sizeof(name), "test_tool_%d", i);
        streaming_emit_tool_start(ctx, id, name);
    }

    TEST_ASSERT_EQUAL_INT(10, ctx->tool_use_count);
    TEST_ASSERT_TRUE(ctx->tool_use_capacity >= 10);

    streaming_context_free(ctx);
}

// =============================================================================
// Completion and Error Tests
// =============================================================================

void test_emit_complete(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_complete(ctx, "end_turn");

    TEST_ASSERT_EQUAL(STREAM_STATE_COMPLETE, ctx->state);
    TEST_ASSERT_EQUAL_STRING("end_turn", ctx->stop_reason);

    streaming_context_free(ctx);
}

void test_emit_complete_with_callback(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_stream_end = test_stream_end_callback;

    streaming_emit_complete(ctx, "tool_use");

    TEST_ASSERT_EQUAL_INT(1, stream_end_count);
    TEST_ASSERT_EQUAL_STRING("tool_use", last_stop_reason);

    streaming_context_free(ctx);
}

void test_emit_error(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    streaming_emit_error(ctx, "Connection timeout");

    TEST_ASSERT_EQUAL(STREAM_STATE_ERROR, ctx->state);
    TEST_ASSERT_EQUAL_STRING("Connection timeout", ctx->error_message);

    streaming_context_free(ctx);
}

void test_emit_error_with_callback(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->on_error = test_error_callback;

    streaming_emit_error(ctx, "API error");

    TEST_ASSERT_EQUAL_INT(1, error_count);
    TEST_ASSERT_EQUAL_STRING("API error", last_error_message);

    streaming_context_free(ctx);
}

// =============================================================================
// Line Buffer Tests
// =============================================================================

void test_line_buffer_growth(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Create a very long line to test buffer growth
    char* long_data = malloc(5000);
    TEST_ASSERT_NOT_NULL(long_data);

    memset(long_data, 'x', 4999);
    long_data[4999] = '\0';

    // Construct SSE data line
    char* sse_line = malloc(5010);
    TEST_ASSERT_NOT_NULL(sse_line);
    snprintf(sse_line, 5010, "data: %s\n", long_data);

    int result = streaming_process_chunk(ctx, sse_line, strlen(sse_line));
    TEST_ASSERT_EQUAL_INT(0, result);

    free(long_data);
    free(sse_line);
    streaming_context_free(ctx);
}

void test_get_last_data(void)
{
    StreamingContext* ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    // Initially no data
    TEST_ASSERT_NULL(streaming_get_last_data(ctx));

    // Add a data line (but don't complete it with newline yet)
    strcpy(ctx->line_buffer, "data: {\"test\":true}");
    ctx->line_buffer_len = strlen(ctx->line_buffer);

    const char* data = streaming_get_last_data(ctx);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_STRING("{\"test\":true}", data);

    streaming_context_free(ctx);
}

// =============================================================================
// Main
// =============================================================================

int main(void)
{
    UNITY_BEGIN();

    // Lifecycle tests
    RUN_TEST(test_streaming_context_create);
    RUN_TEST(test_streaming_context_free_null);
    RUN_TEST(test_streaming_context_reset);

    // SSE parsing tests
    RUN_TEST(test_sse_complete_line);
    RUN_TEST(test_sse_partial_lines);
    RUN_TEST(test_sse_done_signal);
    RUN_TEST(test_sse_event_line);
    RUN_TEST(test_sse_comment_line);
    RUN_TEST(test_sse_empty_line);
    RUN_TEST(test_sse_crlf_handling);
    RUN_TEST(test_sse_multiple_lines_in_chunk);

    // Text emission tests
    RUN_TEST(test_emit_text_accumulates);
    RUN_TEST(test_emit_text_with_callback);
    RUN_TEST(test_emit_text_null_safety);
    RUN_TEST(test_emit_text_large_content);

    // Thinking emission tests
    RUN_TEST(test_emit_thinking_accumulates);
    RUN_TEST(test_emit_thinking_with_callback);

    // Tool use tests
    RUN_TEST(test_emit_tool_start);
    RUN_TEST(test_emit_tool_start_with_callback);
    RUN_TEST(test_emit_tool_delta);
    RUN_TEST(test_emit_tool_delta_with_callback);
    RUN_TEST(test_emit_tool_delta_wrong_id);
    RUN_TEST(test_multiple_tools);
    RUN_TEST(test_tool_capacity_growth);

    // Completion and error tests
    RUN_TEST(test_emit_complete);
    RUN_TEST(test_emit_complete_with_callback);
    RUN_TEST(test_emit_error);
    RUN_TEST(test_emit_error_with_callback);

    // Line buffer tests
    RUN_TEST(test_line_buffer_growth);
    RUN_TEST(test_get_last_data);

    return UNITY_END();
}
