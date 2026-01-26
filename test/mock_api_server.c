#include "mock_api_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

// Simple HTTP response buffer size
#define RESPONSE_BUFFER_SIZE 8192
#define REQUEST_BUFFER_SIZE 4096

// Internal server state
static MockAPIServer* g_server = NULL;

// Thread function for handling incoming connections
static void* server_thread_func(void* arg) {
    MockAPIServer* server = (MockAPIServer*)arg;
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return NULL;
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        return NULL;
    }
    
    // Bind to address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server->port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return NULL;
    }
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        return NULL;
    }
    
    server->is_running = 1;
    
    // Main server loop
    while (server->is_running) {
        if ((client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            if (server->is_running) {
                perror("accept failed");
            }
            break;
        }
        
        // Read the request
        char request[REQUEST_BUFFER_SIZE] = {0};
        ssize_t bytes_read = read(client_fd, request, REQUEST_BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }
        
        // Parse method and endpoint from first line
        char method[16] = {0};
        char endpoint[256] = {0};
        sscanf(request, "%15s %255s", method, endpoint);
        
        // Find matching mock response
        MockAPIResponse* response = NULL;
        for (int i = 0; i < server->response_count; i++) {
            if (strstr(endpoint, server->responses[i].endpoint) != NULL &&
                strcmp(method, server->responses[i].method) == 0) {
                response = &server->responses[i];
                break;
            }
        }
        
        // Handle the request
        if (response == NULL) {
            // No mock response found - return 404
            const char* not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, not_found, strlen(not_found));
        } else if (response->should_fail) {
            // Simulate network failure by closing connection without response
            // Don't send anything, just close
        } else {
            // Simulate delay if specified
            if (response->delay_ms > 0) {
                usleep(response->delay_ms * 1000);
            }

            // Get response body - either from callback or static
            const char* body = response->response_body;
            char* dynamic_body = NULL;

            if (response->callback != NULL) {
                // Extract request body (after double CRLF)
                const char* body_start = strstr(request, "\r\n\r\n");
                if (body_start) {
                    body_start += 4;  // Skip the \r\n\r\n
                }
                dynamic_body = response->callback(body_start, response->callback_data);
                if (dynamic_body) {
                    body = dynamic_body;
                }
            }

            if (body == NULL) {
                body = "{}";  // Empty JSON as fallback
            }

            // Send mock response
            char http_response[RESPONSE_BUFFER_SIZE];
            int content_length = strlen(body);

            snprintf(http_response, sizeof(http_response),
                "HTTP/1.1 %d OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                response->response_code,
                content_length,
                body);

            write(client_fd, http_response, strlen(http_response));

            // Free dynamic body if allocated
            if (dynamic_body) {
                free(dynamic_body);
            }
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return NULL;
}

int mock_api_server_start(MockAPIServer* server) {
    if (server == NULL) {
        return -1;
    }
    
    if (server->is_running) {
        return 0; // Already running
    }
    
    g_server = server;
    server->is_running = 0;
    
    // Start server thread
    if (pthread_create(&server->server_thread, NULL, server_thread_func, server) != 0) {
        perror("pthread_create failed");
        return -1;
    }
    
    return 0;
}

int mock_api_server_stop(MockAPIServer* server) {
    if (server == NULL || !server->is_running) {
        return 0;
    }
    
    server->is_running = 0;
    
    // Wake up the server thread by connecting to it
    int wake_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (wake_fd >= 0) {
        struct sockaddr_in wake_addr;
        wake_addr.sin_family = AF_INET;
        wake_addr.sin_port = htons(server->port);
        wake_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        connect(wake_fd, (struct sockaddr*)&wake_addr, sizeof(wake_addr));
        close(wake_fd);
    }
    
    // Wait for thread to finish
    pthread_join(server->server_thread, NULL);
    
    return 0;
}

int mock_api_server_wait_ready(MockAPIServer* server, int timeout_ms) {
    if (server == NULL) {
        return -1;
    }
    
    int elapsed = 0;
    int check_interval = 10; // Check every 10ms
    
    while (elapsed < timeout_ms) {
        if (server->is_running) {
            return 0; // Server is ready
        }
        
        usleep(check_interval * 1000);
        elapsed += check_interval;
    }
    
    return -1; // Timeout
}

// Helper functions for creating common mock responses
MockAPIResponse mock_openai_tool_response(const char* tool_call_id, const char* content) {
    (void)tool_call_id; // Parameter for future use
    MockAPIResponse response = {0};
    response.endpoint = "/v1/chat/completions";
    response.method = "POST";
    response.response_code = 200;
    response.delay_ms = 0;
    response.should_fail = 0;
    
    // Create OpenAI-style response JSON
    static char response_buffer[2048];
    snprintf(response_buffer, sizeof(response_buffer),
        "{"
        "\"id\":\"chatcmpl-mock123\","
        "\"object\":\"chat.completion\","
        "\"created\":1234567890,"
        "\"model\":\"gpt-3.5-turbo\","
        "\"choices\":["
        "{"
        "\"index\":0,"
        "\"message\":{"
        "\"role\":\"assistant\","
        "\"content\":\"%s\""
        "},"
        "\"finish_reason\":\"stop\""
        "}"
        "],"
        "\"usage\":{"
        "\"prompt_tokens\":50,"
        "\"completion_tokens\":10,"
        "\"total_tokens\":60"
        "}"
        "}",
        content);
    
    response.response_body = response_buffer;
    return response;
}

MockAPIResponse mock_anthropic_tool_response(const char* tool_call_id, const char* content) {
    (void)tool_call_id; // Parameter for future use
    MockAPIResponse response = {0};
    response.endpoint = "/v1/messages";
    response.method = "POST";
    response.response_code = 200;
    response.delay_ms = 0;
    response.should_fail = 0;
    
    // Create Anthropic-style response JSON
    static char response_buffer[2048];
    snprintf(response_buffer, sizeof(response_buffer),
        "{"
        "\"id\":\"msg_mock123\","
        "\"type\":\"message\","
        "\"role\":\"assistant\","
        "\"content\":["
        "{"
        "\"type\":\"text\","
        "\"text\":\"%s\""
        "}"
        "],"
        "\"model\":\"claude-3-sonnet-20240229\","
        "\"stop_reason\":\"end_turn\","
        "\"stop_sequence\":null,"
        "\"usage\":{"
        "\"input_tokens\":50,"
        "\"output_tokens\":10"
        "}"
        "}",
        content);
    
    response.response_body = response_buffer;
    return response;
}

MockAPIResponse mock_error_response(int error_code, const char* error_message) {
    MockAPIResponse response = {0};
    response.endpoint = "/v1/chat/completions";
    response.method = "POST";
    response.response_code = error_code;
    response.delay_ms = 0;
    response.should_fail = 0;
    
    // Create error response JSON
    static char response_buffer[1024];
    snprintf(response_buffer, sizeof(response_buffer),
        "{"
        "\"error\":{"
        "\"message\":\"%s\","
        "\"type\":\"invalid_request_error\","
        "\"code\":\"%d\""
        "}"
        "}",
        error_message, error_code);
    
    response.response_body = response_buffer;
    return response;
}

MockAPIResponse mock_network_failure(void) {
    MockAPIResponse response = {0};
    response.endpoint = "/v1/chat/completions";
    response.method = "POST";
    response.response_code = 0; // Unused for failures
    response.delay_ms = 0;
    response.should_fail = 1; // This causes connection drop
    response.response_body = NULL;
    return response;
}

MockAPIResponse mock_openai_embeddings_response(const float* embedding, size_t dimension) {
    MockAPIResponse response = {0};
    response.endpoint = "/v1/embeddings";
    response.method = "POST";
    response.response_code = 200;
    response.delay_ms = 0;
    response.should_fail = 0;
    response.callback = NULL;
    response.callback_data = NULL;

    // Build embedding array JSON
    static char response_buffer[65536];  // Large buffer for embedding data
    char* ptr = response_buffer;
    size_t remaining = sizeof(response_buffer);
    int written;

    written = snprintf(ptr, remaining,
        "{"
        "\"object\":\"list\","
        "\"data\":["
        "{"
        "\"object\":\"embedding\","
        "\"index\":0,"
        "\"embedding\":[");
    ptr += written;
    remaining -= written;

    // Write embedding values
    for (size_t i = 0; i < dimension && remaining > 50; i++) {
        if (i > 0) {
            written = snprintf(ptr, remaining, ",%.8f", embedding[i]);
        } else {
            written = snprintf(ptr, remaining, "%.8f", embedding[i]);
        }
        ptr += written;
        remaining -= written;
    }

    snprintf(ptr, remaining,
        "]"
        "}"
        "],"
        "\"model\":\"text-embedding-3-small\","
        "\"usage\":{\"prompt_tokens\":5,\"total_tokens\":5}"
        "}");

    response.response_body = response_buffer;
    return response;
}

MockAPIResponse mock_openai_embeddings_dynamic(MockResponseCallback callback_func, void* user_data) {
    MockAPIResponse response = {0};
    response.endpoint = "/v1/embeddings";
    response.method = "POST";
    response.response_code = 200;
    response.delay_ms = 0;
    response.should_fail = 0;
    response.response_body = NULL;  // Will use callback
    response.callback = callback_func;
    response.callback_data = user_data;
    return response;
}