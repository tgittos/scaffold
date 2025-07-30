#ifndef CONVERSATION_TRACKER_H
#define CONVERSATION_TRACKER_H

/**
 * Structure representing a single message in the conversation
 */
typedef struct {
    char *role;     // "user" or "assistant"
    char *content;  // The message content
} ConversationMessage;

/**
 * Structure representing the entire conversation history
 */
typedef struct {
    ConversationMessage *messages;
    int count;
    int capacity;
} ConversationHistory;

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

#endif // CONVERSATION_TRACKER_H