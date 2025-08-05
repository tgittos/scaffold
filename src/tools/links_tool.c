#include "links_tool.h"
#include "embedded_links.h"
#include "memory_tool.h"
#include "json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

// Maximum output size from Links (100KB)
#define MAX_OUTPUT_SIZE (100 * 1024)

// Path to temporary Links binary
static char links_temp_path[256] = {0};
static int links_extracted = 0;

// Extract embedded Links binary to temporary file
static int extract_links_binary() {
    if (links_extracted) return 0;
    
    // Create temporary file
    snprintf(links_temp_path, sizeof(links_temp_path), "/tmp/ralph_links_%d", getpid());
    
    int fd = open(links_temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0700);
    if (fd < 0) {
        perror("Failed to create temporary links binary");
        return -1;
    }
    
    // Write embedded binary data
    size_t written = 0;
    while (written < embedded_links_size) {
        ssize_t ret = write(fd, embedded_links_data + written, embedded_links_size - written);
        if (ret < 0) {
            perror("Failed to write links binary");
            close(fd);
            unlink(links_temp_path);
            return -1;
        }
        written += ret;
    }
    
    close(fd);
    links_extracted = 1;
    return 0;
}

// Clean up temporary file on exit
static void cleanup_links_binary() {
    if (links_extracted && links_temp_path[0]) {
        unlink(links_temp_path);
        links_extracted = 0;
    }
}

// Register cleanup handler
__attribute__((constructor))
static void register_cleanup() {
    atexit(cleanup_links_binary);
}

// Extract URL from JSON arguments
static char* extract_url(const char *arguments) {
    if (!arguments) return NULL;
    
    // Look for "url" parameter
    const char *url_start = strstr(arguments, "\"url\":");
    if (!url_start) return NULL;
    
    url_start += 6;  // Skip "url":
    
    // Skip whitespace
    while (*url_start == ' ' || *url_start == '\t') url_start++;
    
    if (*url_start != '"') return NULL;
    url_start++;  // Skip opening quote
    
    // Find closing quote
    const char *url_end = url_start;
    while (*url_end && *url_end != '"') {
        if (*url_end == '\\' && *(url_end + 1)) {
            url_end += 2;  // Skip escaped character
        } else {
            url_end++;
        }
    }
    
    if (*url_end != '"') return NULL;
    
    // Allocate and copy URL
    size_t len = url_end - url_start;
    char *url = malloc(len + 1);
    if (!url) return NULL;
    
    memcpy(url, url_start, len);
    url[len] = '\0';
    
    return url;
}

// Execute Links in dump mode to fetch a URL
static int fetch_url_with_links(const char *url, char **output) {
    if (!url || !output) return -1;
    
    *output = NULL;
    
    // Create pipes for stdout
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    
    // Fork process
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        
        // Redirect stdout to pipe
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            _exit(1);
        }
        close(pipefd[1]);
        
        // Redirect stderr to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        // Execute extracted links with dump mode
        execl(links_temp_path, "links", "-dump", "-codepage", "utf-8", 
              "-receive-timeout", "30", url, NULL);
        
        // If exec fails
        _exit(1);
    }
    
    // Parent process
    close(pipefd[1]);  // Close write end
    
    // Read output from Links
    char *buffer = malloc(MAX_OUTPUT_SIZE + 1);
    if (!buffer) {
        close(pipefd[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    size_t total_read = 0;
    ssize_t bytes_read;
    
    while (total_read < MAX_OUTPUT_SIZE && 
           (bytes_read = read(pipefd[0], buffer + total_read, 
                             MAX_OUTPUT_SIZE - total_read)) > 0) {
        total_read += bytes_read;
    }
    
    close(pipefd[0]);
    buffer[total_read] = '\0';
    
    // Wait for child process
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        *output = buffer;
        return 0;
    } else {
        free(buffer);
        return -1;
    }
}

int execute_links_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (!tool_call || !result) return -1;
    
    result->tool_call_id = strdup(tool_call->id);
    result->success = 0;
    result->result = NULL;
    
    if (!result->tool_call_id) return -1;
    
    // Extract Links binary if needed
    if (extract_links_binary() != 0) {
        result->result = strdup("Error: Failed to extract embedded Links browser");
        return 0;
    }
    
    // Extract URL from arguments
    char *url = extract_url(tool_call->arguments);
    if (!url) {
        result->result = strdup("Error: Missing or invalid 'url' parameter");
        return 0;
    }
    
    // URL is ready to use, no conversion needed with NSS support
    
    // Fetch URL content
    char *content = NULL;
    int ret = fetch_url_with_links(url, &content);
    
    if (ret == 0 && content) {
        // Success - return the text content
        result->result = content;
        result->success = 1;
        
        // Optionally store important web content in memory
        // Create a summary of the fetched content for memory storage
        size_t content_len = strlen(content);
        if (content_len > 200) {  // Only store if content is substantial
            char summary[512];
            // Take first 200 chars as summary
            snprintf(summary, sizeof(summary), "Web content from %s: %.200s...", url, content);
            
            // Create memory storage request
            ToolCall memory_call = {
                .id = "auto_web_memory",
                .name = "remember",
                .arguments = NULL
            };
            
            // Build the memory arguments
            char* escaped_summary = json_escape_string(summary);
            char* escaped_url = json_escape_string(url);
            if (escaped_summary && escaped_url) {
                size_t args_len = strlen(escaped_summary) + strlen(escaped_url) + 256;
                memory_call.arguments = malloc(args_len);
                if (memory_call.arguments) {
                    snprintf(memory_call.arguments, args_len,
                        "{\"content\": \"%s\", \"type\": \"web_content\", \"source\": \"%s\", \"importance\": \"normal\"}",
                        escaped_summary, escaped_url);
                    
                    // Execute memory storage (ignore result - this is optional)
                    ToolResult memory_result = {0};
                    execute_remember_tool_call(&memory_call, &memory_result);
                    
                    // Clean up memory result
                    if (memory_result.result) free(memory_result.result);
                    if (memory_result.tool_call_id) free(memory_result.tool_call_id);
                    
                    free(memory_call.arguments);
                }
                free(escaped_summary);
                free(escaped_url);
            }
        }
    } else {
        // Error fetching URL
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), 
                "Error: Failed to fetch URL '%s'", url);
        result->result = strdup(error_msg);
    }
    
    free(url);
    return 0;
}

int register_links_tool(ToolRegistry *registry) {
    if (!registry) return -1;
    
    // Reallocate functions array
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 1) * sizeof(ToolFunction));
    if (!new_functions) return -1;
    
    registry->functions = new_functions;
    ToolFunction *func = &registry->functions[registry->function_count];
    
    // Set function details
    func->name = strdup("web_fetch");
    func->description = strdup("Fetch web page content using bundled Links browser in text mode");
    func->parameter_count = 1;
    
    // Allocate parameters
    func->parameters = malloc(sizeof(ToolParameter));
    if (!func->parameters) {
        free(func->name);
        free(func->description);
        return -1;
    }
    
    // URL parameter
    func->parameters[0].name = strdup("url");
    func->parameters[0].type = strdup("string");
    func->parameters[0].description = strdup("The URL to fetch");
    func->parameters[0].required = 1;
    func->parameters[0].enum_values = NULL;
    func->parameters[0].enum_count = 0;
    
    registry->function_count++;
    
    return 0;
}