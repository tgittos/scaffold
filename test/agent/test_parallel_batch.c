#include "unity/unity.h"
#include "lib/agent/tool_batch_executor.h"
#include "lib/agent/session.h"
#include "lib/tools/tool_cache.h"
#include "lib/policy/verified_file_context.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

static AgentSession session;
static ToolOrchestrationContext orchestration;
static ToolBatchContext batch_ctx;

static int64_t time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int slow_tool_execute(const ToolCall *call, ToolResult *result) {
    int ms = 100;
    if (call->arguments) {
        const char *p = strstr(call->arguments, "\"ms\":");
        if (p) ms = atoi(p + 5);
    }
    usleep(ms * 1000);
    result->tool_call_id = call->id ? strdup(call->id) : NULL;
    result->result = strdup("{\"ok\": true}");
    result->success = 1;
    return 0;
}

static void register_slow_tool(ToolRegistry *reg, const char *name) {
    ToolFunction func = {0};
    func.name = strdup(name);
    func.description = strdup("test tool");
    func.execute_func = slow_tool_execute;
    func.thread_safe = 1;
    func.parameter_count = 0;
    func.parameters = NULL;
    ToolFunctionArray_push(&reg->functions, func);
}

void setUp(void) {
    memset(&session, 0, sizeof(session));
    ToolFunctionArray_init(&session.tools.functions);
    session.tools.cache = tool_cache_create();
    register_slow_tool(&session.tools, "slow_a");
    register_slow_tool(&session.tools, "slow_b");
    register_slow_tool(&session.tools, "slow_c");

    memset(&orchestration, 0, sizeof(orchestration));
    tool_orchestration_init(&orchestration, NULL);

    batch_ctx.session = &session;
    batch_ctx.orchestration = &orchestration;
}

void tearDown(void) {
    tool_orchestration_cleanup(&orchestration);
    tool_cache_destroy(session.tools.cache);
    session.tools.cache = NULL;
    for (size_t i = 0; i < session.tools.functions.count; i++) {
        free(session.tools.functions.data[i].name);
        free(session.tools.functions.data[i].description);
    }
    ToolFunctionArray_destroy(&session.tools.functions);
}

static ToolCall make_call(const char *id, const char *name, const char *args) {
    return (ToolCall){
        .id = strdup(id),
        .name = strdup(name),
        .arguments = args ? strdup(args) : NULL
    };
}

static void free_calls(ToolCall *calls, int n) {
    for (int i = 0; i < n; i++) {
        free(calls[i].id);
        free(calls[i].name);
        free(calls[i].arguments);
    }
}

static void free_results(ToolResult *results, int n) {
    for (int i = 0; i < n; i++) {
        free(results[i].tool_call_id);
        free(results[i].result);
    }
}

void test_parallel_two_tools_faster_than_sequential(void) {
    ToolCall calls[2] = {
        make_call("c1", "slow_a", "{\"ms\": 150}"),
        make_call("c2", "slow_b", "{\"ms\": 150}")
    };
    ToolResult results[2] = {0};
    int executed = 0;

    int64_t start = time_ms();
    int rc = tool_batch_execute(&batch_ctx, calls, 2, results, NULL, &executed);
    int64_t elapsed = time_ms() - start;

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, executed);
    TEST_ASSERT_EQUAL_INT(1, results[0].success);
    TEST_ASSERT_EQUAL_INT(1, results[1].success);
    /* Parallel: ~150ms not ~300ms. Use generous upper bound for CI. */
    TEST_ASSERT_LESS_THAN_INT64(280, elapsed);

    free_calls(calls, 2);
    free_results(results, 2);
}

void test_three_tools_parallel(void) {
    ToolCall calls[3] = {
        make_call("c1", "slow_a", "{\"ms\": 100}"),
        make_call("c2", "slow_b", "{\"ms\": 100}"),
        make_call("c3", "slow_c", "{\"ms\": 100}")
    };
    ToolResult results[3] = {0};
    int executed = 0;

    int64_t start = time_ms();
    int rc = tool_batch_execute(&batch_ctx, calls, 3, results, NULL, &executed);
    int64_t elapsed = time_ms() - start;

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(3, executed);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(1, results[i].success);
    }
    /* 3x100ms sequential=300ms, parallel≈100ms. Cap at 220ms. */
    TEST_ASSERT_LESS_THAN_INT64(220, elapsed);

    free_calls(calls, 3);
    free_results(results, 3);
}

void test_single_tool_inline(void) {
    ToolCall calls[1] = {
        make_call("c1", "slow_a", "{\"ms\": 50}")
    };
    ToolResult results[1] = {0};
    int executed = 0;

    int rc = tool_batch_execute(&batch_ctx, calls, 1, results, NULL, &executed);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, executed);
    TEST_ASSERT_EQUAL_INT(1, results[0].success);

    free_calls(calls, 1);
    free_results(results, 1);
}

void test_compact_mode_parallel(void) {
    ToolCall calls[2] = {
        make_call("c1", "slow_a", "{\"ms\": 100}"),
        make_call("c2", "slow_b", "{\"ms\": 100}")
    };
    ToolResult results[2] = {0};
    int call_indices[2] = {0};
    int executed = 0;

    int64_t start = time_ms();
    int rc = tool_batch_execute(&batch_ctx, calls, 2, results, call_indices, &executed);
    int64_t elapsed = time_ms() - start;

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, executed);
    TEST_ASSERT_EQUAL_INT(0, call_indices[0]);
    TEST_ASSERT_EQUAL_INT(1, call_indices[1]);
    TEST_ASSERT_LESS_THAN_INT64(200, elapsed);

    free_calls(calls, 2);
    free_results(results, 2);
}

void test_results_in_correct_slots(void) {
    ToolCall calls[2] = {
        make_call("c1", "slow_a", "{\"ms\": 150}"),
        make_call("c2", "slow_b", "{\"ms\": 50}")
    };
    ToolResult results[2] = {0};
    int executed = 0;

    int rc = tool_batch_execute(&batch_ctx, calls, 2, results, NULL, &executed);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, executed);
    TEST_ASSERT_EQUAL_STRING("c1", results[0].tool_call_id);
    TEST_ASSERT_EQUAL_STRING("c2", results[1].tool_call_id);

    free_calls(calls, 2);
    free_results(results, 2);
}

struct cache_thread_data {
    ToolCache *cache;
    int thread_id;
};

#define CACHE_THREADS 4
#define CACHE_OPS_PER_THREAD 200

static void *cache_hammer(void *arg) {
    struct cache_thread_data *td = (struct cache_thread_data *)arg;
    char name[64], args[64], result[64];
    for (int i = 0; i < CACHE_OPS_PER_THREAD; i++) {
        snprintf(name, sizeof(name), "tool_%d_%d", td->thread_id, i);
        snprintf(args, sizeof(args), "{\"i\": %d}", i);
        snprintf(result, sizeof(result), "result_%d_%d", td->thread_id, i);
        tool_cache_store(td->cache, name, args, result, 1);
        tool_cache_lookup(td->cache, name, args);
    }
    return NULL;
}

void test_tool_cache_thread_safety(void) {
    ToolCache *cache = tool_cache_create();
    TEST_ASSERT_NOT_NULL(cache);

    pthread_t threads[CACHE_THREADS];
    struct cache_thread_data tdata[CACHE_THREADS];

    for (int i = 0; i < CACHE_THREADS; i++) {
        tdata[i].cache = cache;
        tdata[i].thread_id = i;
        pthread_create(&threads[i], NULL, cache_hammer, &tdata[i]);
    }
    for (int i = 0; i < CACHE_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    TEST_ASSERT_TRUE(cache->count > 0);

    tool_cache_destroy(cache);
}

void test_non_thread_safe_falls_back_to_serial(void) {
    /* Register a non-thread-safe tool */
    ToolFunction unsafe = {0};
    unsafe.name = strdup("unsafe_tool");
    unsafe.description = strdup("not thread safe");
    unsafe.execute_func = slow_tool_execute;
    unsafe.thread_safe = 0;
    ToolFunctionArray_push(&session.tools.functions, unsafe);

    ToolCall calls[2] = {
        make_call("c1", "slow_a", "{\"ms\": 100}"),
        make_call("c2", "unsafe_tool", "{\"ms\": 100}")
    };
    ToolResult results[2] = {0};
    int executed = 0;

    int64_t start = time_ms();
    int rc = tool_batch_execute(&batch_ctx, calls, 2, results, NULL, &executed);
    int64_t elapsed = time_ms() - start;

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, executed);
    TEST_ASSERT_EQUAL_INT(1, results[0].success);
    TEST_ASSERT_EQUAL_INT(1, results[1].success);
    /* Serial: should take ~200ms (100+100), not ~100ms */
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(180, elapsed);

    free_calls(calls, 2);
    free_results(results, 2);
}

void test_null_context_returns_error(void) {
    int executed = 0;
    ToolResult results[1] = {0};
    ToolCall calls[1] = { make_call("c1", "slow_a", NULL) };

    int rc = tool_batch_execute(NULL, calls, 1, results, NULL, &executed);
    TEST_ASSERT_EQUAL_INT(-1, rc);

    free_calls(calls, 1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parallel_two_tools_faster_than_sequential);
    RUN_TEST(test_three_tools_parallel);
    RUN_TEST(test_single_tool_inline);
    RUN_TEST(test_compact_mode_parallel);
    RUN_TEST(test_results_in_correct_slots);
    RUN_TEST(test_non_thread_safe_falls_back_to_serial);
    RUN_TEST(test_tool_cache_thread_safety);
    RUN_TEST(test_null_context_returns_error);
    return UNITY_END();
}
