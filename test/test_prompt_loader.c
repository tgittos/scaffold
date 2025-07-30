#include "unity.h"
#include "prompt_loader.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void setUp(void) {
    // Clean up any existing test files
    unlink("PROMPT.md");
    unlink("test_prompt.md");
}

void tearDown(void) {
    // Clean up test files
    unlink("PROMPT.md");
    unlink("test_prompt.md");
}

void test_load_system_prompt_with_null_parameter(void) {
    int result = load_system_prompt(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_load_system_prompt_file_not_exists(void) {
    char *prompt_content = NULL;
    
    // Ensure PROMPT.md doesn't exist
    unlink("PROMPT.md");
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(-1, result);
    TEST_ASSERT_NULL(prompt_content);
}

void test_load_system_prompt_simple_content(void) {
    char *prompt_content = NULL;
    
    // Create test PROMPT.md file
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "You are a helpful assistant.");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    TEST_ASSERT_EQUAL_STRING("You are a helpful assistant.", prompt_content);
    
    cleanup_system_prompt(&prompt_content);
    TEST_ASSERT_NULL(prompt_content);
}

void test_load_system_prompt_with_trailing_newlines(void) {
    char *prompt_content = NULL;
    
    // Create test PROMPT.md file with trailing newlines
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "You are a helpful assistant.\n\n\n");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    TEST_ASSERT_EQUAL_STRING("You are a helpful assistant.", prompt_content);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_multiline_content(void) {
    char *prompt_content = NULL;
    
    // Create test PROMPT.md file with multiline content
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "You are a helpful assistant.\nAlways be polite and informative.\nRespond concisely.");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    TEST_ASSERT_EQUAL_STRING("You are a helpful assistant.\nAlways be polite and informative.\nRespond concisely.", prompt_content);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_empty_file(void) {
    char *prompt_content = NULL;
    
    // Create empty PROMPT.md file
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    TEST_ASSERT_EQUAL_STRING("", prompt_content);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_with_whitespace_only(void) {
    char *prompt_content = NULL;
    
    // Create PROMPT.md file with only whitespace
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "   \n\t\n  \r\n");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    TEST_ASSERT_EQUAL_STRING("", prompt_content);
    
    cleanup_system_prompt(&prompt_content);
}

void test_cleanup_system_prompt_with_null_pointer(void) {
    // Should not crash
    cleanup_system_prompt(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_cleanup_system_prompt_with_null_content(void) {
    char *prompt_content = NULL;
    
    // Should not crash
    cleanup_system_prompt(&prompt_content);
    TEST_ASSERT_NULL(prompt_content);
}

void test_cleanup_system_prompt_with_allocated_content(void) {
    char *prompt_content = NULL;
    
    // Create test PROMPT.md file
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Test content");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    cleanup_system_prompt(&prompt_content);
    TEST_ASSERT_NULL(prompt_content);
}

void test_load_system_prompt_large_content(void) {
    char *prompt_content = NULL;
    
    // Create test PROMPT.md file with larger content
    FILE *test_file = fopen("PROMPT.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write a reasonably large prompt
    for (int i = 0; i < 100; i++) {
        fprintf(test_file, "Line %d: You are a helpful assistant with detailed knowledge. ", i);
    }
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    TEST_ASSERT_TRUE(strlen(prompt_content) > 1000); // Should be a large string
    
    cleanup_system_prompt(&prompt_content);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_load_system_prompt_with_null_parameter);
    RUN_TEST(test_load_system_prompt_file_not_exists);
    RUN_TEST(test_load_system_prompt_simple_content);
    RUN_TEST(test_load_system_prompt_with_trailing_newlines);
    RUN_TEST(test_load_system_prompt_multiline_content);
    RUN_TEST(test_load_system_prompt_empty_file);
    RUN_TEST(test_load_system_prompt_with_whitespace_only);
    RUN_TEST(test_cleanup_system_prompt_with_null_pointer);
    RUN_TEST(test_cleanup_system_prompt_with_null_content);
    RUN_TEST(test_cleanup_system_prompt_with_allocated_content);
    RUN_TEST(test_load_system_prompt_large_content);
    
    return UNITY_END();
}