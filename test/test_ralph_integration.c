#include "unity/unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

void setUp(void) {
    // Setup code if needed
}

void tearDown(void) {
    // Teardown code if needed
}

// Helper function to execute Ralph and capture both stdout and stderr
int execute_ralph_and_capture(const char* message, char* output_buffer, size_t buffer_size) {
    int stdout_pipe[2], stderr_pipe[2];
    pid_t pid;
    
    // Create pipes for capturing both stdout and stderr
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        return -1;
    }
    
    pid = fork();
    if (pid == -1) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return -1;
    } else if (pid == 0) {
        // Child process
        close(stdout_pipe[0]); // Close read ends
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO); // Redirect stdout to pipe
        dup2(stderr_pipe[1], STDERR_FILENO); // Redirect stderr to pipe  
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Execute Ralph with the message (with debug flag to see all output)
        execl("./ralph", "ralph", "--debug", message, (char*)NULL);
        exit(1); // Should never reach here if execl succeeds
    } else {
        // Parent process
        close(stdout_pipe[1]); // Close write ends
        close(stderr_pipe[1]);
        
        // Read from both pipes
        char stdout_buf[4096] = {0};
        char stderr_buf[4096] = {0};
        
        ssize_t stdout_bytes = read(stdout_pipe[0], stdout_buf, sizeof(stdout_buf) - 1);
        ssize_t stderr_bytes = read(stderr_pipe[0], stderr_buf, sizeof(stderr_buf) - 1);
        
        // Combine output, preferring stdout first, then stderr
        size_t total_written = 0;
        if (stdout_bytes > 0 && total_written < buffer_size - 1) {
            size_t to_copy = ((size_t)stdout_bytes < (buffer_size - 1 - total_written)) ? 
                            (size_t)stdout_bytes : (buffer_size - 1 - total_written);
            memcpy(output_buffer + total_written, stdout_buf, to_copy);
            total_written += to_copy;
        }
        if (stderr_bytes > 0 && total_written < buffer_size - 1) {
            size_t to_copy = ((size_t)stderr_bytes < (buffer_size - 1 - total_written)) ? 
                            (size_t)stderr_bytes : (buffer_size - 1 - total_written);
            memcpy(output_buffer + total_written, stderr_buf, to_copy);
            total_written += to_copy;
        }
        output_buffer[total_written] = '\0';
        
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        // Wait for child to finish
        int status;
        waitpid(pid, &status, 0);
        
        return WEXITSTATUS(status);
    }
}

void test_ralph_file_summarization_functionality(void) {
    // Check if Ralph executable exists
    struct stat st;
    if (stat("./ralph", &st) != 0) {
        TEST_IGNORE_MESSAGE("Ralph executable not found. Run 'make' first.");
        return;
    }
    
    // Test message requesting file summarization
    const char* test_message = "Summarize the most important source code file in ./src";
    char output_buffer[8192] = {0};
    
    printf("\n=== Testing Ralph Integration ===\n");
    printf("Executing: ./ralph \"%s\"\n", test_message);
    
    // Execute Ralph and capture output
    int exit_code = execute_ralph_and_capture(test_message, output_buffer, sizeof(output_buffer));
    
    printf("Exit code: %d\n", exit_code);
    printf("Output length: %zu bytes\n", strlen(output_buffer));
    printf("First 200 chars of output: %.200s%s\n", output_buffer, strlen(output_buffer) > 200 ? "..." : "");
    
    // Test that Ralph executed successfully (exit code 0)
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, exit_code, "Ralph should exit successfully");
    
    // Test that we got some output
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, strlen(output_buffer), "Ralph should produce output");
    
    // Test that output contains content suggesting a summary was generated
    // Look for keywords that would indicate a file summary response
    bool has_summary_content = (
        strstr(output_buffer, "file") != NULL ||
        strstr(output_buffer, "code") != NULL ||
        strstr(output_buffer, "function") != NULL ||
        strstr(output_buffer, "implementation") != NULL ||
        strstr(output_buffer, "contains") != NULL ||
        strstr(output_buffer, "main") != NULL ||
        strstr(output_buffer, "defines") != NULL ||
        strstr(output_buffer, "responsible") != NULL ||
        strstr(output_buffer, "handles") != NULL ||
        strstr(output_buffer, "manages") != NULL
    );
    
    if (!has_summary_content) {
        printf("\n=== FAILURE ANALYSIS ===\n");
        printf("Full output:\n%s\n", output_buffer);
        printf("=== END FAILURE ANALYSIS ===\n");
    }
    
    TEST_ASSERT_TRUE_MESSAGE(has_summary_content, 
        "Output should contain summary-related content (file, code, function, implementation, etc.)");
    
    // Test that output is reasonably substantial (more than just an error message)
    TEST_ASSERT_GREATER_THAN_MESSAGE(50, strlen(output_buffer), 
        "Summary should be reasonably substantial (>50 characters)");
    
    printf("=== Test PASSED: Ralph successfully provided file summary ===\n");
}

void test_ralph_executable_basic_functionality(void) {
    // Check if Ralph executable exists
    struct stat st;
    if (stat("./ralph", &st) != 0) {
        TEST_IGNORE_MESSAGE("Ralph executable not found. Run 'make' first.");
        return;
    }
    
    // Test basic functionality with simple message
    const char* test_message = "Say hello";
    char output_buffer[4096] = {0};
    
    int exit_code = execute_ralph_and_capture(test_message, output_buffer, sizeof(output_buffer));
    
    // Test that Ralph executed successfully
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, exit_code, "Ralph should execute basic commands successfully");
    
    // Test that we got some output
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, strlen(output_buffer), "Ralph should produce output for basic commands");
}

int main(void) {
    UNITY_BEGIN();
    
    printf("=== Ralph Integration Test Suite ===\n");
    printf("Testing actual Ralph executable functionality...\n");
    
    RUN_TEST(test_ralph_executable_basic_functionality);
    RUN_TEST(test_ralph_file_summarization_functionality);
    
    return UNITY_END();
}