/**
 * Mock Embeddings Server Helper
 *
 * Combines mock_api_server with mock_embeddings to provide
 * a complete mocking solution for OpenAI embeddings API.
 *
 * Usage:
 *   1. Call mock_embeddings_init_test_groups() in setUp()
 *   2. Start mock server with mock_embeddings_server_callback
 *   3. Set OPENAI_API_URL to http://127.0.0.1:<port>/v1/embeddings
 *   4. Run tests
 *   5. Stop server and call mock_embeddings_cleanup() in tearDown()
 */

#ifndef MOCK_EMBEDDINGS_SERVER_H
#define MOCK_EMBEDDINGS_SERVER_H

#include "mock_api_server.h"

/**
 * Callback function for mock_openai_embeddings_dynamic().
 * Parses OpenAI embeddings request JSON, extracts the input text,
 * generates mock embedding, and returns properly formatted response.
 *
 * @param request_body JSON request body from client
 * @param user_data Unused (pass NULL)
 * @return Dynamically allocated JSON response (caller frees)
 */
char* mock_embeddings_server_callback(const char* request_body, void* user_data);

/**
 * Helper to set up a complete mock embeddings server.
 * Returns a MockAPIResponse configured for dynamic embedding generation.
 */
MockAPIResponse mock_embeddings_server_response(void);

#endif // MOCK_EMBEDDINGS_SERVER_H
