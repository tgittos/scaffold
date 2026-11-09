#include "../../test/unity/unity.h"
#include "ui/status_line.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define MODEL "claude-sonnet-4-5"
#define DIM_MODEL "\033[2m" MODEL "\033[0m"

static char buf[512];

void setUp(void) {
    status_line_init();
    unsetenv("COLUMNS");
    buf[0] = '\0';
}

void tearDown(void) {
    status_line_cleanup();
}

static const char *format_with(const char *model, int window, int prompt_tokens) {
    status_line_set_model(model, window);
    if (prompt_tokens > 0) status_line_update_tokens(prompt_tokens, 0);
    status_line_format_context(buf, sizeof(buf));
    return buf;
}

void test_format_no_usage_shows_question(void) {
    TEST_ASSERT_EQUAL_STRING("?/200k  " DIM_MODEL, format_with(MODEL, 200000, 0));
}

void test_format_12_percent(void) {
    TEST_ASSERT_EQUAL_STRING("12.3%/200k  " DIM_MODEL, format_with(MODEL, 200000, 24600));
}

void test_format_seventy_boundary_plain(void) {
    TEST_ASSERT_EQUAL_STRING("70.0%/200k  " DIM_MODEL, format_with(MODEL, 200000, 140000));
}

void test_format_yellow_above_seventy(void) {
    TEST_ASSERT_EQUAL_STRING("\033[93m70.5%/200k\033[0m  " DIM_MODEL,
                             format_with(MODEL, 200000, 141000));
}

void test_format_ninety_boundary_yellow(void) {
    TEST_ASSERT_EQUAL_STRING("\033[93m90.0%/200k\033[0m  " DIM_MODEL,
                             format_with(MODEL, 200000, 180000));
}

void test_format_red_above_ninety(void) {
    TEST_ASSERT_EQUAL_STRING("\033[91m90.5%/200k\033[0m  " DIM_MODEL,
                             format_with(MODEL, 200000, 181000));
}

void test_format_uncapped_over_100(void) {
    TEST_ASSERT_EQUAL_STRING("\033[91m125.0%/200k\033[0m  " DIM_MODEL,
                             format_with(MODEL, 200000, 250000));
}

void test_format_window_zero_shows_bare_question(void) {
    TEST_ASSERT_EQUAL_STRING("?  " DIM_MODEL, format_with(MODEL, 0, 24600));
}

void test_format_no_model_ctx_only(void) {
    TEST_ASSERT_EQUAL_STRING("12.3%/200k", format_with(NULL, 200000, 24600));
}

void test_format_nothing_known_is_empty(void) {
    TEST_ASSERT_EQUAL_STRING("", format_with(NULL, 0, 0));
}

void test_format_window_token_formats(void) {
    TEST_ASSERT_EQUAL_STRING("?/8.2k  \033[2mm\033[0m", format_with("m", 8192, 0));
    TEST_ASSERT_EQUAL_STRING("?/10k  \033[2mm\033[0m", format_with("m", 10000, 0));
    TEST_ASSERT_EQUAL_STRING("?/1.0k  \033[2mm\033[0m", format_with("m", 1000, 0));
    TEST_ASSERT_EQUAL_STRING("?/999  \033[2mm\033[0m", format_with("m", 999, 0));
    TEST_ASSERT_EQUAL_STRING("?/1.0M  \033[2mm\033[0m", format_with("m", 1048576, 0));
    TEST_ASSERT_EQUAL_STRING("?/10M  \033[2mm\033[0m", format_with("m", 10000000, 0));
}

void test_format_long_model_name_truncates(void) {
    char model[128] = {0};
    memset(model, 'x', 120);
    model[120] = '\0';
    format_with(model, 200000, 0);
    const char *start = strstr(buf, "\033[2m");
    TEST_ASSERT_NOT_NULL(start);
    start += strlen("\033[2m");
    const char *end = strstr(start, "\033[0m");
    TEST_ASSERT_NOT_NULL(end);
    TEST_ASSERT_EQUAL_INT(95, (int)(end - start));
    TEST_ASSERT_NULL(strchr(buf, '\n'));
}

void test_format_small_buffer_truncates_safely(void) {
    char small[8];
    status_line_set_model(MODEL, 200000);
    status_line_format_context(small, sizeof(small));
    TEST_ASSERT_EQUAL_INT(7, (int)strlen(small));
}

void test_invalidate_resets_to_question(void) {
    status_line_set_model(MODEL, 200000);
    status_line_update_tokens(24600, 0);
    status_line_invalidate_context();
    status_line_format_context(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("?/200k  " DIM_MODEL, buf);
}

void test_set_model_switch_updates_segment(void) {
    status_line_set_model(MODEL, 200000);
    status_line_update_tokens(24600, 0);
    status_line_set_model("gpt-4o", 128000);
    status_line_format_context(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("19.2%/128k  \033[2mgpt-4o\033[0m", buf);
}

static int g_saved_stdout = -1;
static char g_capture_path[64];

static void capture_begin(void) {
    fflush(stdout);
    g_saved_stdout = dup(STDOUT_FILENO);
    snprintf(g_capture_path, sizeof(g_capture_path), "/tmp/test_status_line.XXXXXX");
    int fd = mkstemp(g_capture_path);
    TEST_ASSERT_TRUE(fd >= 0);
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

static const char *capture_end(void) {
    fflush(stdout);
    dup2(g_saved_stdout, STDOUT_FILENO);
    close(g_saved_stdout);
    g_saved_stdout = -1;

    FILE *in = fopen(g_capture_path, "r");
    TEST_ASSERT_NOT_NULL(in);
    size_t n = fread(buf, 1, sizeof(buf) - 1, in);
    buf[n] = '\0';
    fclose(in);
    remove(g_capture_path);
    return buf;
}

static const char *render_capture(void) {
    capture_begin();
    status_line_render_info();
    return capture_end();
}

static int count_char(const char *s, char c) {
    int n = 0;
    for (; *s; s++) if (*s == c) n++;
    return n;
}

void test_render_idle_renders_footer(void) {
    status_line_set_model(MODEL, 200000);
    TEST_ASSERT_EQUAL_STRING("  ?/200k  " DIM_MODEL "\n", render_capture());
}

void test_render_with_usage_after_tokens(void) {
    status_line_set_model(MODEL, 200000);
    status_line_update_tokens(24600, 0);
    status_line_set_last_response_tokens(500);
    const char *out = render_capture();
    TEST_ASSERT_NOT_NULL(strstr(out, "500 tokens"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\033[0m   12.3%/200k  " DIM_MODEL "\n"));
    TEST_ASSERT_EQUAL_INT(1, count_char(out, '\n'));
}

void test_render_busy_appends_segment(void) {
    status_line_set_model(MODEL, 200000);
    status_line_update_tokens(24600, 0);
    capture_begin();
    status_line_set_busy("building");
    capture_end();
    const char *out = render_capture();
    TEST_ASSERT_EQUAL_INT(0, strncmp(out, "\033[2m  \xe2\x97\x8f building (", 15));
    TEST_ASSERT_NOT_NULL(strstr(out, "\033[0m   12.3%/200k  " DIM_MODEL "\n"));
    TEST_ASSERT_EQUAL_INT(1, count_char(out, '\n'));
}

void test_render_nothing_known_prints_nothing(void) {
    TEST_ASSERT_EQUAL_STRING("", render_capture());
}

void test_render_narrow_drops_model(void) {
    setenv("COLUMNS", "20", 1);
    status_line_set_model(MODEL, 200000);
    TEST_ASSERT_EQUAL_STRING("  ?/200k\n", render_capture());
}

void test_render_fits_exactly_keeps_model(void) {
    setenv("COLUMNS", "28", 1);
    status_line_set_model(MODEL, 200000);
    TEST_ASSERT_EQUAL_STRING("  ?/200k  " DIM_MODEL "\n", render_capture());
}

void test_render_too_narrow_drops_segment(void) {
    setenv("COLUMNS", "22", 1);
    status_line_set_model(MODEL, 200000);
    status_line_update_tokens(24600, 0);
    capture_begin();
    status_line_set_busy("building");
    capture_end();
    const char *out = render_capture();
    TEST_ASSERT_NULL(strstr(out, "12.3%"));
    TEST_ASSERT_NOT_NULL(strstr(out, "building"));
    TEST_ASSERT_EQUAL_INT(1, count_char(out, '\n'));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_format_no_usage_shows_question);
    RUN_TEST(test_format_12_percent);
    RUN_TEST(test_format_seventy_boundary_plain);
    RUN_TEST(test_format_yellow_above_seventy);
    RUN_TEST(test_format_ninety_boundary_yellow);
    RUN_TEST(test_format_red_above_ninety);
    RUN_TEST(test_format_uncapped_over_100);
    RUN_TEST(test_format_window_zero_shows_bare_question);
    RUN_TEST(test_format_no_model_ctx_only);
    RUN_TEST(test_format_nothing_known_is_empty);
    RUN_TEST(test_format_window_token_formats);
    RUN_TEST(test_format_long_model_name_truncates);
    RUN_TEST(test_format_small_buffer_truncates_safely);
    RUN_TEST(test_invalidate_resets_to_question);
    RUN_TEST(test_set_model_switch_updates_segment);
    RUN_TEST(test_render_idle_renders_footer);
    RUN_TEST(test_render_with_usage_after_tokens);
    RUN_TEST(test_render_busy_appends_segment);
    RUN_TEST(test_render_nothing_known_prints_nothing);
    RUN_TEST(test_render_narrow_drops_model);
    RUN_TEST(test_render_fits_exactly_keeps_model);
    RUN_TEST(test_render_too_narrow_drops_segment);
    return UNITY_END();
}
