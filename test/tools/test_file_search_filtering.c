#include "unity.h"
#include "file_tools.h"
#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void setUp(void) {}
void tearDown(void) {}

// Test that file_search skips directories that should be excluded
void test_file_search_skips_git_directory(void) {
    // Create test directory structure
    mkdir("filter_test", 0755);
    mkdir("filter_test/.git", 0755);
    mkdir("filter_test/src", 0755);

    // Create files
    FILE* src_file = fopen("filter_test/src/code.c", "w");
    TEST_ASSERT_NOT_NULL(src_file);
    fprintf(src_file, "// search_target in src\n");
    fclose(src_file);

    FILE* git_file = fopen("filter_test/.git/config", "w");
    TEST_ASSERT_NOT_NULL(git_file);
    fprintf(git_file, "search_target in .git\n");
    fclose(git_file);

    // Search for the pattern
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test", "search_target", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in src, not in .git
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "src/code.c") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test/.git/config");
    unlink("filter_test/src/code.c");
    rmdir("filter_test/.git");
    rmdir("filter_test/src");
    rmdir("filter_test");
}

// Test that file_search skips node_modules directory
void test_file_search_skips_node_modules(void) {
    // Create test directory structure
    mkdir("filter_test2", 0755);
    mkdir("filter_test2/node_modules", 0755);
    mkdir("filter_test2/lib", 0755);

    // Create files
    FILE* lib_file = fopen("filter_test2/lib/app.js", "w");
    TEST_ASSERT_NOT_NULL(lib_file);
    fprintf(lib_file, "// findme_pattern in lib\n");
    fclose(lib_file);

    FILE* nm_file = fopen("filter_test2/node_modules/package.js", "w");
    TEST_ASSERT_NOT_NULL(nm_file);
    fprintf(nm_file, "// findme_pattern in node_modules\n");
    fclose(nm_file);

    // Search for the pattern
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test2", "findme_pattern", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in lib, not in node_modules
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "lib/app.js") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test2/node_modules/package.js");
    unlink("filter_test2/lib/app.js");
    rmdir("filter_test2/node_modules");
    rmdir("filter_test2/lib");
    rmdir("filter_test2");
}

// Test that file_search skips binary files by extension
void test_file_search_skips_binary_extensions(void) {
    // Create test directory structure
    mkdir("filter_test3", 0755);

    // Create a text file
    FILE* txt_file = fopen("filter_test3/readme.txt", "w");
    TEST_ASSERT_NOT_NULL(txt_file);
    fprintf(txt_file, "binary_marker in text\n");
    fclose(txt_file);

    // Create a "binary" file (just use .exe extension)
    FILE* exe_file = fopen("filter_test3/program.exe", "w");
    TEST_ASSERT_NOT_NULL(exe_file);
    fprintf(exe_file, "binary_marker in exe\n");
    fclose(exe_file);

    // Create a "binary" image file
    FILE* png_file = fopen("filter_test3/image.png", "w");
    TEST_ASSERT_NOT_NULL(png_file);
    fprintf(png_file, "binary_marker in png\n");
    fclose(png_file);

    // Search for the pattern
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test3", "binary_marker", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in readme.txt, not in .exe or .png
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "readme.txt") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test3/readme.txt");
    unlink("filter_test3/program.exe");
    unlink("filter_test3/image.png");
    rmdir("filter_test3");
}

// Test that file_search works with file pattern filter
void test_file_search_with_file_pattern(void) {
    // Create test directory structure
    mkdir("filter_test4", 0755);

    // Create files with different extensions
    FILE* c_file = fopen("filter_test4/main.c", "w");
    TEST_ASSERT_NOT_NULL(c_file);
    fprintf(c_file, "pattern_test in C\n");
    fclose(c_file);

    FILE* h_file = fopen("filter_test4/header.h", "w");
    TEST_ASSERT_NOT_NULL(h_file);
    fprintf(h_file, "pattern_test in H\n");
    fclose(h_file);

    FILE* js_file = fopen("filter_test4/app.js", "w");
    TEST_ASSERT_NOT_NULL(js_file);
    fprintf(js_file, "pattern_test in JS\n");
    fclose(js_file);

    // Search only in .c files
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test4", "pattern_test", "*.c", 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in main.c
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "main.c") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test4/main.c");
    unlink("filter_test4/header.h");
    unlink("filter_test4/app.js");
    rmdir("filter_test4");
}

// Test that file_search handles binary content (null bytes)
void test_file_search_skips_binary_content(void) {
    // Create test directory structure
    mkdir("filter_test5", 0755);

    // Create a text file
    FILE* txt_file = fopen("filter_test5/text.txt", "w");
    TEST_ASSERT_NOT_NULL(txt_file);
    fprintf(txt_file, "null_test_marker in text\n");
    fclose(txt_file);

    // Create a binary file with null bytes
    FILE* bin_file = fopen("filter_test5/data.bin", "wb");
    TEST_ASSERT_NOT_NULL(bin_file);
    // Write some text followed by null bytes
    const char* bin_content = "null_test_marker\0\0\0binary data";
    fwrite(bin_content, 1, 30, bin_file);
    fclose(bin_file);

    // Search for the pattern
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test5", "null_test_marker", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in text.txt because data.bin contains null bytes
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "text.txt") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test5/text.txt");
    unlink("filter_test5/data.bin");
    rmdir("filter_test5");
}

// Test that file_search does not fail when encountering large files
void test_file_search_handles_large_files_gracefully(void) {
    // Create test directory structure
    mkdir("filter_test6", 0755);

    // Create a small text file
    FILE* small_file = fopen("filter_test6/small.txt", "w");
    TEST_ASSERT_NOT_NULL(small_file);
    fprintf(small_file, "large_file_test_marker\n");
    fclose(small_file);

    // Create a large file (over 1MB threshold)
    FILE* large_file = fopen("filter_test6/large.txt", "w");
    TEST_ASSERT_NOT_NULL(large_file);
    // Write 1.5MB of data
    for (int i = 0; i < 50000; i++) {
        fprintf(large_file, "large_file_test_marker padding line %d with lots of extra text\n", i);
    }
    fclose(large_file);

    // Search for the pattern - should not fail
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test6", "large_file_test_marker", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in small.txt, large.txt should be skipped
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "small.txt") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test6/small.txt");
    unlink("filter_test6/large.txt");
    rmdir("filter_test6");
}

// Test that file_search skips hidden files and directories
void test_file_search_skips_hidden_files(void) {
    // Create test directory structure
    mkdir("filter_test7", 0755);
    mkdir("filter_test7/.hidden_dir", 0755);

    // Create visible file
    FILE* visible = fopen("filter_test7/visible.txt", "w");
    TEST_ASSERT_NOT_NULL(visible);
    fprintf(visible, "hidden_test_marker in visible\n");
    fclose(visible);

    // Create hidden file
    FILE* hidden = fopen("filter_test7/.hidden_file", "w");
    TEST_ASSERT_NOT_NULL(hidden);
    fprintf(hidden, "hidden_test_marker in hidden\n");
    fclose(hidden);

    // Create file in hidden directory
    FILE* in_hidden_dir = fopen("filter_test7/.hidden_dir/file.txt", "w");
    TEST_ASSERT_NOT_NULL(in_hidden_dir);
    fprintf(in_hidden_dir, "hidden_test_marker in hidden dir\n");
    fclose(in_hidden_dir);

    // Search for the pattern
    SearchResults results = {0};
    FileErrorCode result = file_search_content("filter_test7", "hidden_test_marker", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    // Should find only in visible.txt
    TEST_ASSERT_EQUAL_INT(1, results.count);
    TEST_ASSERT_TRUE(strstr(results.results[0].file_path, "visible.txt") != NULL);

    // Cleanup
    cleanup_search_results(&results);
    unlink("filter_test7/visible.txt");
    unlink("filter_test7/.hidden_file");
    unlink("filter_test7/.hidden_dir/file.txt");
    rmdir("filter_test7/.hidden_dir");
    rmdir("filter_test7");
}

// Test search on root directory does not fail
void test_file_search_on_current_directory(void) {
    // Create a test file in current directory
    FILE* test_file = fopen("test_root_search.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "unique_root_search_marker_xyz123\n");
    fclose(test_file);

    // Search for the pattern from current directory
    SearchResults results = {0};
    FileErrorCode result = file_search_content(".", "unique_root_search_marker_xyz123", NULL, 1, 1, &results);

    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    TEST_ASSERT_TRUE(results.count >= 1);

    // Should find our test file
    int found = 0;
    for (int i = 0; i < results.count; i++) {
        if (strstr(results.results[i].file_path, "test_root_search.txt") != NULL) {
            found = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);

    // Cleanup
    cleanup_search_results(&results);
    unlink("test_root_search.txt");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_file_search_skips_git_directory);
    RUN_TEST(test_file_search_skips_node_modules);
    RUN_TEST(test_file_search_skips_binary_extensions);
    RUN_TEST(test_file_search_with_file_pattern);
    RUN_TEST(test_file_search_skips_binary_content);
    RUN_TEST(test_file_search_handles_large_files_gracefully);
    RUN_TEST(test_file_search_skips_hidden_files);
    RUN_TEST(test_file_search_on_current_directory);

    return UNITY_END();
}
