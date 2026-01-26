#ifndef CONVERSATION_TRACKER_H
#define CONVERSATION_TRACKER_H

#include "../utils/darray.h"

/**
 * Structure representing a single message in the conversation
 */
typedef struct {
    char *role;        // "user", "assistant", or "tool"
    char *content;     // The message content
    char *tool_call_id; // For tool messages, the ID of the tool call being responded to (nullable)
    char *tool_name;   // For tool messages, the name of the tool that was called (nullable)
} ConversationMessage;

/**
 * Structure representing the entire conversation history
 */
DARRAY_DECLARE(ConversationHistory, ConversationMessage)

/**
 * Load conversation history from CONVERSATION.md file
 * 
 * @param history Pointer to ConversationHistory structure to populate
 * @return 0 on success, -1 on failure (file read error, memory allocation error)
 *         Returns 0 if file doesn't exist (empty history)
 */
int load_conversation_history(ConversationHistory *history);

/**
 * Append a new message to the conversation history and save to CONVERSATION.md
 * 
 * @param history Pointer to ConversationHistory structure
 * @param role The role of the message sender ("user" or "assistant")
 * @param content The message content
 * @return 0 on success, -1 on failure (memory allocation error, file write error)
 */
int append_conversation_message(ConversationHistory *history, const char *role, const char *content);

/**
 * Append a tool message to the conversation history and save to CONVERSATION.md
 * 
 * @param history Pointer to ConversationHistory structure
 * @param content The tool response content
 * @param tool_call_id The ID of the tool call being responded to
 * @param tool_name The name of the tool that was called
 * @return 0 on success, -1 on failure (memory allocation error, file write error)
 */
int append_tool_message(ConversationHistory *history, const char *content, const char *tool_call_id, const char *tool_name);

/**
 * Free all memory allocated for conversation history
 * 
 * @param history Pointer to ConversationHistory structure to cleanup
 */
void cleanup_conversation_history(ConversationHistory *history);

/**
 * Initialize an empty conversation history structure
 * 
 * @param history Pointer to ConversationHistory structure to initialize
 */
void init_conversation_history(ConversationHistory *history);

/**
 * Load extended conversation history from vector database
 * 
 * @param history Pointer to ConversationHistory structure to populate
 * @param days_back Number of days to look back (0 for all history)
 * @param max_messages Maximum number of messages to retrieve
 * @return 0 on success, -1 on failure
 */
int load_extended_conversation_history(ConversationHistory *history, int days_back, size_t max_messages);

/**
 * Search conversation history for relevant messages
 * 
 * @param query The search query text
 * @param max_results Maximum number of results to return
 * @return ConversationHistory structure with relevant messages, or NULL on failure
 *         Caller must free with cleanup_conversation_history
 */
ConversationHistory* search_conversation_history(const char *query, size_t max_results);

#endif // CONVERSATION_TRACKER_H