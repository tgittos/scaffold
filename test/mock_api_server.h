#ifndef MOCK_API_SERVER_H
#define MOCK_API_SERVER_H

#include <pthread.h>

// Forward declaration for callback
struct MockAPIResponse;

// Callback type for dynamic response generation
// Receives request body, returns dynamically allocated response body (caller frees)
typedef char* (*MockResponseCallback)(const char* request_body, void* user_data);

// Mock API response structure
typedef struct MockAPIResponse {
    const char* endpoint;           // API endpoint to mock (e.g., "/v1/chat/completions")
    const char* method;             // HTTP method (GET, POST, etc.)
    const char* response_body;      // JSON response body to return (static)
    int response_code;              // HTTP status code to return
    int delay_ms;                   // Delay before responding (simulate network latency)
    int should_fail;                // If 1, drop connection instead of responding
    MockResponseCallback callback;  // Optional callback for dynamic responses
    void* callback_data;            // User data passed to callback
} MockAPIResponse;

// Mock server configuration
typedef struct {
    int port;                       // Port to listen on
    MockAPIResponse* responses;     // Array of mock responses
    int response_count;             // Number of mock responses
    int is_running;                 // Server status
    pthread_t server_thread;        // Server thread handle
} MockAPIServer;

// Mock server functions
int mock_api_server_start(MockAPIServer* server);
int mock_api_server_stop(MockAPIServer* server);
int mock_api_server_wait_ready(MockAPIServer* server, int timeout_ms);

// Helper functions for creating mock responses
MockAPIResponse mock_openai_tool_response(const char* tool_call_id, const char* content);
MockAPIResponse mock_anthropic_tool_response(const char* tool_call_id, const char* content);
MockAPIResponse mock_error_response(int error_code, const char* error_message);
MockAPIResponse mock_network_failure(void);

// Embeddings mock support
MockAPIResponse mock_openai_embeddings_response(const float* embedding, size_t dimension);

// Dynamic embeddings mock - uses callback to generate embeddings based on input text
// The callback_func should parse the input JSON, extract text, and return embedding JSON
MockAPIResponse mock_openai_embeddings_dynamic(MockResponseCallback callback_func, void* user_data);

// Test helper macros
#define MOCK_SERVER_DEFAULT_PORT 8888
#define MOCK_SERVER_MAX_RESPONSES 16

#endif // MOCK_API_SERVER_H