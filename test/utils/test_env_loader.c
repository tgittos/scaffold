#include "unity/unity.h"
#include "env_loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

void setUp(void)
{
    // Clean up before each test
}

void tearDown(void)
{
    // Clean up after each test
    unsetenv("TEST_VAR");
    unsetenv("TEST_VAR2");
    unsetenv("SPACES_VAR");
    unsetenv("EMPTY_VAR");
}

void test_load_env_file_with_null_filepath(void)
{
    int result = load_env_file(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_load_env_file_nonexistent_file(void)
{
    // Should return 0 (success) when file doesn't exist - .env is optional
    int result = load_env_file("nonexistent.env");
    TEST_ASSERT_EQUAL(0, result);
}

void test_load_env_file_basic_functionality(void)
{
    // Create a temporary .env file
    FILE *temp_file = fopen("test.env", "w");
    TEST_ASSERT_NOT_NULL(temp_file);
    
    fprintf(temp_file, "TEST_VAR=test_value\n");
    fprintf(temp_file, "TEST_VAR2=another_value\n");
    fclose(temp_file);
    
    // Load the file
    int result = load_env_file("test.env");
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that variables were set
    const char *value1 = getenv("TEST_VAR");
    const char *value2 = getenv("TEST_VAR2");
    
    TEST_ASSERT_NOT_NULL(value1);
    TEST_ASSERT_NOT_NULL(value2);
    TEST_ASSERT_EQUAL_STRING("test_value", value1);
    TEST_ASSERT_EQUAL_STRING("another_value", value2);
    
    // Clean up
    unlink("test.env");
}

void test_load_env_file_with_whitespace(void)
{
    // Create a temporary .env file with whitespace
    FILE *temp_file = fopen("test_whitespace.env", "w");
    TEST_ASSERT_NOT_NULL(temp_file);
    
    fprintf(temp_file, "  SPACES_VAR  =  value_with_spaces  \n");
    fprintf(temp_file, "\tTEST_VAR\t=\tvalue_with_tabs\t\n");
    fclose(temp_file);
    
    // Load the file
    int result = load_env_file("test_whitespace.env");
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that variables were set with trimmed whitespace
    const char *spaces_value = getenv("SPACES_VAR");
    const char *tabs_value = getenv("TEST_VAR");
    
    TEST_ASSERT_NOT_NULL(spaces_value);
    TEST_ASSERT_NOT_NULL(tabs_value);
    TEST_ASSERT_EQUAL_STRING("value_with_spaces", spaces_value);
    TEST_ASSERT_EQUAL_STRING("value_with_tabs", tabs_value);
    
    // Clean up
    unlink("test_whitespace.env");
}

void test_load_env_file_with_comments_and_empty_lines(void)
{
    // Create a temporary .env file with comments and empty lines
    FILE *temp_file = fopen("test_comments.env", "w");
    TEST_ASSERT_NOT_NULL(temp_file);
    
    fprintf(temp_file, "# This is a comment\n");
    fprintf(temp_file, "\n");
    fprintf(temp_file, "TEST_VAR=test_value\n");
    fprintf(temp_file, "   # Another comment with spaces\n");
    fprintf(temp_file, "\n");
    fprintf(temp_file, "TEST_VAR2=another_value\n");
    fclose(temp_file);
    
    // Load the file
    int result = load_env_file("test_comments.env");
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that only the non-comment variables were set
    const char *value1 = getenv("TEST_VAR");
    const char *value2 = getenv("TEST_VAR2");
    
    TEST_ASSERT_NOT_NULL(value1);
    TEST_ASSERT_NOT_NULL(value2);
    TEST_ASSERT_EQUAL_STRING("test_value", value1);
    TEST_ASSERT_EQUAL_STRING("another_value", value2);
    
    // Clean up
    unlink("test_comments.env");
}

void test_load_env_file_with_empty_values(void)
{
    // Create a temporary .env file with empty values
    FILE *temp_file = fopen("test_empty.env", "w");
    TEST_ASSERT_NOT_NULL(temp_file);
    
    fprintf(temp_file, "EMPTY_VAR=\n");
    fprintf(temp_file, "TEST_VAR=not_empty\n");
    fclose(temp_file);
    
    // Load the file
    int result = load_env_file("test_empty.env");
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that variables were set
    const char *empty_value = getenv("EMPTY_VAR");
    const char *normal_value = getenv("TEST_VAR");
    
    TEST_ASSERT_NOT_NULL(empty_value);
    TEST_ASSERT_NOT_NULL(normal_value);
    TEST_ASSERT_EQUAL_STRING("", empty_value);
    TEST_ASSERT_EQUAL_STRING("not_empty", normal_value);
    
    // Clean up
    unlink("test_empty.env");
}

void test_load_env_file_with_invalid_lines(void)
{
    // Create a temporary .env file with invalid lines (no equals sign)
    FILE *temp_file = fopen("test_invalid.env", "w");
    TEST_ASSERT_NOT_NULL(temp_file);
    
    fprintf(temp_file, "INVALID_LINE_NO_EQUALS\n");
    fprintf(temp_file, "TEST_VAR=valid_value\n");
    fprintf(temp_file, "ANOTHER_INVALID_LINE\n");
    fclose(temp_file);
    
    // Load the file - should succeed, invalid lines are skipped
    int result = load_env_file("test_invalid.env");
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that only the valid variable was set
    const char *value = getenv("TEST_VAR");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_STRING("valid_value", value);
    
    // Invalid variables should not be set
    const char *invalid1 = getenv("INVALID_LINE_NO_EQUALS");
    const char *invalid2 = getenv("ANOTHER_INVALID_LINE");
    TEST_ASSERT_NULL(invalid1);
    TEST_ASSERT_NULL(invalid2);
    
    // Clean up
    unlink("test_invalid.env");
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_load_env_file_with_null_filepath);
    RUN_TEST(test_load_env_file_nonexistent_file);
    RUN_TEST(test_load_env_file_basic_functionality);
    RUN_TEST(test_load_env_file_with_whitespace);
    RUN_TEST(test_load_env_file_with_comments_and_empty_lines);
    RUN_TEST(test_load_env_file_with_empty_values);
    RUN_TEST(test_load_env_file_with_invalid_lines);
    
    return UNITY_END();
}