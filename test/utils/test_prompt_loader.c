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
    
    // Should succeed with just the core system prompt
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain the core system prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "User customization:") != NULL);
    
    cleanup_system_prompt(&prompt_content);
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
    
    // Should contain both core prompt and user prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "User customization:") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are a helpful assistant.") != NULL);
    
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
    
    // Should contain core prompt and trimmed user prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are a helpful assistant.") != NULL);
    // Should not contain the trailing newlines
    TEST_ASSERT_TRUE(strstr(prompt_content, "assistant.\n\n") == NULL);
    
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
    
    // Should contain core prompt and full multiline user prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are a helpful assistant.\nAlways be polite and informative.\nRespond concisely.") != NULL);
    
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
    
    // Should contain only the core system prompt (no user content)
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "User customization:") != NULL);
    
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
    
    // Should contain only the core system prompt (whitespace trimmed away)
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "User customization:") != NULL);
    
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
    
    // Should contain core prompt plus the large user content
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Line 0: You are a helpful assistant") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Line 99: You are a helpful assistant") != NULL);
    TEST_ASSERT_TRUE(strlen(prompt_content) > 1000); // Should be a large string
    
    cleanup_system_prompt(&prompt_content);
}

void test_core_system_prompt_always_present(void) {
    char *prompt_content = NULL;
    
    // Test with no file
    unlink("PROMPT.md");
    int result = load_system_prompt(&prompt_content);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Core prompt should always be present
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are an advanced AI programming agent") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "access to powerful tools") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "User customization:") != NULL);
    
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
    RUN_TEST(test_core_system_prompt_always_present);
    
    return UNITY_END();
}