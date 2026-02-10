#include "network/api_common.h"
#include "network/image_attachment.h"
#include "session/conversation_tracker.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal valid PNG: 8-byte signature + IHDR + IEND */
static const unsigned char TINY_PNG[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, /* PNG signature */
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, /* IHDR chunk */
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, /* 1x1 pixel */
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, /* RGB, CRC */
    0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, /* IDAT chunk */
    0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, /* compressed */
    0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, /* data + CRC */
    0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, /* IEND chunk */
    0x44, 0xAE, 0x42, 0x60, 0x82
};
#define TINY_PNG_SIZE sizeof(TINY_PNG)

static char test_png_path[256];
static char test_jpg_path[256];

void setUp(void) {
    snprintf(test_png_path, sizeof(test_png_path), "/tmp/test_image_XXXXXX.png");
    int fd = mkstemps(test_png_path, 4);
    if (fd >= 0) {
        write(fd, TINY_PNG, TINY_PNG_SIZE);
        close(fd);
    }

    snprintf(test_jpg_path, sizeof(test_jpg_path), "/tmp/test_image_XXXXXX.jpg");
    fd = mkstemps(test_jpg_path, 4);
    if (fd >= 0) {
        write(fd, TINY_PNG, TINY_PNG_SIZE);
        close(fd);
    }
}

void tearDown(void) {
    remove(test_png_path);
    remove(test_jpg_path);
    api_common_clear_pending_images();
}

void test_no_images(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse("Hello world", &result));
    TEST_ASSERT_EQUAL(0, result.count);
    TEST_ASSERT_NOT_NULL(result.cleaned_text);
    TEST_ASSERT_EQUAL_STRING("Hello world", result.cleaned_text);
    image_attachment_cleanup(&result);
}

void test_single_image(void) {
    char input[512];
    snprintf(input, sizeof(input), "Look at @%s please", test_png_path);

    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse(input, &result));
    TEST_ASSERT_EQUAL(1, result.count);
    TEST_ASSERT_NOT_NULL(result.items[0].filename);
    TEST_ASSERT_NOT_NULL(result.items[0].mime_type);
    TEST_ASSERT_EQUAL_STRING("image/png", result.items[0].mime_type);
    TEST_ASSERT_NOT_NULL(result.items[0].base64_data);
    TEST_ASSERT_TRUE(strlen(result.items[0].base64_data) > 0);

    /* Cleaned text should have [image: filename] placeholder */
    TEST_ASSERT_NOT_NULL(strstr(result.cleaned_text, "[image:"));
    TEST_ASSERT_NULL(strstr(result.cleaned_text, "@"));

    image_attachment_cleanup(&result);
}

void test_multiple_images(void) {
    char input[1024];
    snprintf(input, sizeof(input), "Compare @%s and @%s", test_png_path, test_jpg_path);

    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse(input, &result));
    TEST_ASSERT_EQUAL(2, result.count);
    TEST_ASSERT_EQUAL_STRING("image/png", result.items[0].mime_type);
    TEST_ASSERT_EQUAL_STRING("image/jpeg", result.items[1].mime_type);

    /* Both placeholders should be in cleaned text */
    char *first = strstr(result.cleaned_text, "[image:");
    TEST_ASSERT_NOT_NULL(first);
    char *second = strstr(first + 1, "[image:");
    TEST_ASSERT_NOT_NULL(second);

    image_attachment_cleanup(&result);
}

void test_missing_file(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse("Look at @nonexistent.png", &result));
    TEST_ASSERT_EQUAL(0, result.count);
    /* @nonexistent.png should remain in cleaned text as-is since we skip failed loads
       by advancing one char at a time */
    TEST_ASSERT_NOT_NULL(result.cleaned_text);
    image_attachment_cleanup(&result);
}

void test_non_image_at_ref(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse("Email @user about this", &result));
    TEST_ASSERT_EQUAL(0, result.count);
    TEST_ASSERT_EQUAL_STRING("Email @user about this", result.cleaned_text);
    image_attachment_cleanup(&result);
}

void test_mime_detection_png(void) {
    char input[512];
    snprintf(input, sizeof(input), "@%s", test_png_path);

    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse(input, &result));
    TEST_ASSERT_EQUAL(1, result.count);
    TEST_ASSERT_EQUAL_STRING("image/png", result.items[0].mime_type);
    image_attachment_cleanup(&result);
}

void test_mime_detection_jpg(void) {
    char input[512];
    snprintf(input, sizeof(input), "@%s", test_jpg_path);

    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse(input, &result));
    TEST_ASSERT_EQUAL(1, result.count);
    TEST_ASSERT_EQUAL_STRING("image/jpeg", result.items[0].mime_type);
    image_attachment_cleanup(&result);
}

void test_null_input(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(-1, image_attachment_parse(NULL, &result));
    TEST_ASSERT_EQUAL(-1, image_attachment_parse("text", NULL));
}

void test_openai_format(void) {
    char input[512];
    snprintf(input, sizeof(input), "Describe @%s", test_png_path);

    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse(input, &result));
    TEST_ASSERT_EQUAL(1, result.count);

    api_common_set_pending_images(result.items, result.count);

    /* Build a minimal conversation and format with OpenAI formatter */
    ConversationHistory history;
    init_conversation_history(&history);

    char buffer[8192];
    int written = build_messages_json(buffer, sizeof(buffer), NULL, &history,
                                      result.cleaned_text, format_openai_message, 0);
    TEST_ASSERT_TRUE(written > 0);

    /* Verify OpenAI image_url format */
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"image_url\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"url\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "data:image/png;base64,"));

    cleanup_conversation_history(&history);
    api_common_clear_pending_images();
    image_attachment_cleanup(&result);
}

void test_anthropic_format(void) {
    char input[512];
    snprintf(input, sizeof(input), "Describe @%s", test_png_path);

    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse(input, &result));
    TEST_ASSERT_EQUAL(1, result.count);

    api_common_set_pending_images(result.items, result.count);

    ConversationHistory history;
    init_conversation_history(&history);

    char buffer[8192];
    int written = build_messages_json(buffer, sizeof(buffer), NULL, &history,
                                      result.cleaned_text, format_anthropic_message, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Verify Anthropic image format */
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"type\":\"image\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"source\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"type\":\"base64\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"media_type\":\"image/png\""));

    cleanup_conversation_history(&history);
    api_common_clear_pending_images();
    image_attachment_cleanup(&result);
}

void test_cleanup_idempotent(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse("no images here", &result));
    image_attachment_cleanup(&result);
    /* Second cleanup should not crash */
    image_attachment_cleanup(&result);
}

void test_at_sign_at_end_of_string(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse("trailing @", &result));
    TEST_ASSERT_EQUAL(0, result.count);
    TEST_ASSERT_EQUAL_STRING("trailing @", result.cleaned_text);
    image_attachment_cleanup(&result);
}

void test_empty_string(void) {
    ImageParseResult result;
    TEST_ASSERT_EQUAL(0, image_attachment_parse("", &result));
    TEST_ASSERT_EQUAL(0, result.count);
    TEST_ASSERT_EQUAL_STRING("", result.cleaned_text);
    image_attachment_cleanup(&result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_images);
    RUN_TEST(test_single_image);
    RUN_TEST(test_multiple_images);
    RUN_TEST(test_missing_file);
    RUN_TEST(test_non_image_at_ref);
    RUN_TEST(test_mime_detection_png);
    RUN_TEST(test_mime_detection_jpg);
    RUN_TEST(test_null_input);
    RUN_TEST(test_openai_format);
    RUN_TEST(test_anthropic_format);
    RUN_TEST(test_cleanup_idempotent);
    RUN_TEST(test_at_sign_at_end_of_string);
    RUN_TEST(test_empty_string);
    return UNITY_END();
}
