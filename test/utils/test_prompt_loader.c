#include "unity.h"
#include "util/prompt_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "util/app_home.h"
#include <prompt_data.h>

// Store original directory to restore after tests
static char original_dir[4096];
static const char *test_dir = "/tmp/test_prompt_loader_XXXXXX";
static char test_dir_path[256];

void setUp(void) {
    app_home_init(NULL);
    // Save current directory
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        TEST_FAIL_MESSAGE("Failed to get current directory");
    }

    // Create a unique temp directory for this test
    strcpy(test_dir_path, test_dir);
    if (mkdtemp(test_dir_path) == NULL) {
        TEST_FAIL_MESSAGE("Failed to create temp directory");
    }

    // Change to temp directory
    if (chdir(test_dir_path) != 0) {
        TEST_FAIL_MESSAGE("Failed to change to temp directory");
    }
}

void tearDown(void) {
    // Clean up test files in temp directory
    unlink("AGENTS.md");

    // Return to original directory
    if (chdir(original_dir) != 0) {
        // Can't use TEST_FAIL here as tearDown shouldn't fail tests
        fprintf(stderr, "Warning: Failed to return to original directory\n");
    }

    // Remove temp directory
    rmdir(test_dir_path);

    app_home_cleanup();
}

void test_load_system_prompt_with_null_parameter(void) {
    int result = load_system_prompt(NULL, NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_load_system_prompt_file_not_exists(void) {
    char *prompt_content = NULL;
    
    // Ensure AGENTS.md doesn't exist
    unlink("AGENTS.md");
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    // Should succeed with just the core system prompt
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain the core system prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "# User Instructions") != NULL);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_simple_content(void) {
    char *prompt_content = NULL;
    
    // Create test AGENTS.md file
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "You are a helpful assistant.");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain both core prompt and user prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "# User Instructions") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are a helpful assistant.") != NULL);
    
    cleanup_system_prompt(&prompt_content);
    TEST_ASSERT_NULL(prompt_content);
}

void test_load_system_prompt_with_trailing_newlines(void) {
    char *prompt_content = NULL;
    
    // Create test AGENTS.md file with trailing newlines
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "You are a helpful assistant.\n\n\n");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain core prompt and trimmed user prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are a helpful assistant.") != NULL);
    // Should not contain the trailing newlines
    TEST_ASSERT_TRUE(strstr(prompt_content, "assistant.\n\n") == NULL);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_multiline_content(void) {
    char *prompt_content = NULL;
    
    // Create test AGENTS.md file with multiline content
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "You are a helpful assistant.\nAlways be polite and informative.\nRespond concisely.");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain core prompt and full multiline user prompt
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "You are a helpful assistant.\nAlways be polite and informative.\nRespond concisely.") != NULL);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_empty_file(void) {
    char *prompt_content = NULL;
    
    // Create empty AGENTS.md file
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain only the core system prompt (no user content)
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "# User Instructions") != NULL);
    
    cleanup_system_prompt(&prompt_content);
}

void test_load_system_prompt_with_whitespace_only(void) {
    char *prompt_content = NULL;
    
    // Create AGENTS.md file with only whitespace
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "   \n\t\n  \r\n");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain only the core system prompt (whitespace trimmed away)
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "# User Instructions") != NULL);
    
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
    
    // Create test AGENTS.md file
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Test content");
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    cleanup_system_prompt(&prompt_content);
    TEST_ASSERT_NULL(prompt_content);
}

void test_load_system_prompt_large_content(void) {
    char *prompt_content = NULL;
    
    // Create test AGENTS.md file with larger content
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write a reasonably large prompt
    for (int i = 0; i < 100; i++) {
        fprintf(test_file, "Line %d: You are a helpful assistant with detailed knowledge. ", i);
    }
    fclose(test_file);
    
    int result = load_system_prompt(&prompt_content, NULL);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);
    
    // Should contain core prompt plus the large user content
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Line 0: You are a helpful assistant") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Line 99: You are a helpful assistant") != NULL);
    TEST_ASSERT_TRUE(strlen(prompt_content) > 1000); // Should be a large string
    
    cleanup_system_prompt(&prompt_content);
}

void test_core_system_prompt_always_present(void) {
    char *prompt_content = NULL;

    // Test with no file
    unlink("AGENTS.md");
    int result = load_system_prompt(&prompt_content, NULL);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // Core prompt should always be present
    TEST_ASSERT_TRUE(strstr(prompt_content, SYSTEM_PROMPT_TEXT) != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "# User Instructions") != NULL);

    cleanup_system_prompt(&prompt_content);
}

void test_file_reference_expansion_simple(void) {
    char *prompt_content = NULL;

    // Create a referenced file
    FILE *ref_file = fopen("DETAILS.md", "w");
    TEST_ASSERT_NOT_NULL(ref_file);
    fprintf(ref_file, "This is the details content.");
    fclose(ref_file);

    // Create AGENTS.md with a file reference
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "See @DETAILS.md for more info.");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // Should contain the expanded content with file tags
    TEST_ASSERT_TRUE(strstr(prompt_content, "<file name=\"DETAILS.md\">") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "This is the details content.") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "</file>") != NULL);
    // Original @DETAILS.md should be replaced
    TEST_ASSERT_TRUE(strstr(prompt_content, "@DETAILS.md") == NULL);

    cleanup_system_prompt(&prompt_content);
    unlink("DETAILS.md");
}

void test_file_reference_missing_file_silent_fail(void) {
    char *prompt_content = NULL;

    // Create AGENTS.md with a reference to non-existent file
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "See @NONEXISTENT.md for details.");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // The @NONEXISTENT.md should remain unchanged (silent fail)
    TEST_ASSERT_TRUE(strstr(prompt_content, "@NONEXISTENT.md") != NULL);

    cleanup_system_prompt(&prompt_content);
}

void test_file_reference_multiple_references(void) {
    char *prompt_content = NULL;

    // Create referenced files
    FILE *file1 = fopen("FILE1.md", "w");
    TEST_ASSERT_NOT_NULL(file1);
    fprintf(file1, "Content from file 1.");
    fclose(file1);

    FILE *file2 = fopen("FILE2.md", "w");
    TEST_ASSERT_NOT_NULL(file2);
    fprintf(file2, "Content from file 2.");
    fclose(file2);

    // Create AGENTS.md with multiple references
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "First: @FILE1.md\nSecond: @FILE2.md");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // Both files should be expanded
    TEST_ASSERT_TRUE(strstr(prompt_content, "<file name=\"FILE1.md\">") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Content from file 1.") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "<file name=\"FILE2.md\">") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Content from file 2.") != NULL);

    cleanup_system_prompt(&prompt_content);
    unlink("FILE1.md");
    unlink("FILE2.md");
}

void test_file_reference_with_subdirectory(void) {
    char *prompt_content = NULL;

    // Create subdirectory and file
    mkdir("subdir", 0755);
    FILE *ref_file = fopen("subdir/NESTED.md", "w");
    TEST_ASSERT_NOT_NULL(ref_file);
    fprintf(ref_file, "Nested file content.");
    fclose(ref_file);

    // Create AGENTS.md with subdirectory reference
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "See @subdir/NESTED.md for nested content.");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // Should expand the nested file
    TEST_ASSERT_TRUE(strstr(prompt_content, "<file name=\"subdir/NESTED.md\">") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "Nested file content.") != NULL);

    cleanup_system_prompt(&prompt_content);
    unlink("subdir/NESTED.md");
    rmdir("subdir");
}

void test_file_reference_no_recursive_expansion(void) {
    char *prompt_content = NULL;

    // Create a file that itself contains an @ reference
    FILE *ref_file = fopen("OUTER.md", "w");
    TEST_ASSERT_NOT_NULL(ref_file);
    fprintf(ref_file, "This references @INNER.md which should NOT be expanded.");
    fclose(ref_file);

    // Create the inner file (should not be expanded)
    FILE *inner_file = fopen("INNER.md", "w");
    TEST_ASSERT_NOT_NULL(inner_file);
    fprintf(inner_file, "Inner content that should not appear.");
    fclose(inner_file);

    // Create AGENTS.md referencing the outer file
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "See @OUTER.md for info.");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // OUTER.md should be expanded
    TEST_ASSERT_TRUE(strstr(prompt_content, "<file name=\"OUTER.md\">") != NULL);
    // The @INNER.md reference should remain as-is (not expanded)
    TEST_ASSERT_TRUE(strstr(prompt_content, "@INNER.md") != NULL);
    // Inner content should NOT appear
    TEST_ASSERT_TRUE(strstr(prompt_content, "Inner content that should not appear.") == NULL);

    cleanup_system_prompt(&prompt_content);
    unlink("OUTER.md");
    unlink("INNER.md");
}

void test_file_reference_mixed_existing_and_missing(void) {
    char *prompt_content = NULL;

    // Create only one of the referenced files
    FILE *ref_file = fopen("EXISTS.md", "w");
    TEST_ASSERT_NOT_NULL(ref_file);
    fprintf(ref_file, "This file exists.");
    fclose(ref_file);

    // Create AGENTS.md with mixed references
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "File 1: @EXISTS.md\nFile 2: @MISSING.md");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // EXISTS.md should be expanded
    TEST_ASSERT_TRUE(strstr(prompt_content, "<file name=\"EXISTS.md\">") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "This file exists.") != NULL);
    // MISSING.md should remain as @MISSING.md
    TEST_ASSERT_TRUE(strstr(prompt_content, "@MISSING.md") != NULL);

    cleanup_system_prompt(&prompt_content);
    unlink("EXISTS.md");
}

void test_file_reference_at_sign_not_filename(void) {
    char *prompt_content = NULL;

    // Create AGENTS.md with @ signs that aren't file references
    FILE *test_file = fopen("AGENTS.md", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Email: user@example.com\nTwitter: @handle\nPrice: $5 @ store");
    fclose(test_file);

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // These should remain unchanged (not treated as file references)
    TEST_ASSERT_TRUE(strstr(prompt_content, "user@example.com") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "@handle") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "$5 @ store") != NULL);

    cleanup_system_prompt(&prompt_content);
}

void test_platform_info_present(void) {
    char *prompt_content = NULL;

    // Ensure no AGENTS.md file
    unlink("AGENTS.md");

    int result = load_system_prompt(&prompt_content, NULL);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(prompt_content);

    // Platform information section should be present
    TEST_ASSERT_TRUE(strstr(prompt_content, "## Platform Information:") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "- Architecture:") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "- Operating System:") != NULL);
    TEST_ASSERT_TRUE(strstr(prompt_content, "- Working Directory:") != NULL);

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

    // File reference expansion tests
    RUN_TEST(test_file_reference_expansion_simple);
    RUN_TEST(test_file_reference_missing_file_silent_fail);
    RUN_TEST(test_file_reference_multiple_references);
    RUN_TEST(test_file_reference_with_subdirectory);
    RUN_TEST(test_file_reference_no_recursive_expansion);
    RUN_TEST(test_file_reference_mixed_existing_and_missing);
    RUN_TEST(test_file_reference_at_sign_not_filename);

    // Platform information tests
    RUN_TEST(test_platform_info_present);

    return UNITY_END();
}