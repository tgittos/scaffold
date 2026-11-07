#ifndef DEBUG_OUTPUT_H
#define DEBUG_OUTPUT_H

// Summarizes large numeric arrays (e.g. embeddings) for readable debug output.
// Caller must free the returned string.
char* debug_summarize_json(const char *json);

#endif // DEBUG_OUTPUT_H
