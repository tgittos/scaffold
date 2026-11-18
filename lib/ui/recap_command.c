#include "recap_command.h"
#include "../agent/session.h"
#include <stdio.h>

int process_recap_command(const char *args, AgentSession *session) {
    (void)args;
    if (!session) {
        fprintf(stderr, "No active session for /recap\n");
        return -1;
    }
    int result = session_generate_recap(session, 0);
    if (result != 0) {
        fprintf(stderr, "Failed to generate recap\n");
        return -1;
    }
    return 0;
}
