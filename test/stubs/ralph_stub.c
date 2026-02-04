/**
 * Session stub for unit tests that don't need full session functionality.
 * Provides a controllable mock of session_process_message().
 */

#include "lib/agent/session.h"
#include <stdatomic.h>
#include <unistd.h>

static atomic_int g_stub_return_value = 0;
static atomic_int g_stub_delay_ms = 0;

void session_stub_set_return_value(int value) {
    atomic_store(&g_stub_return_value, value);
}

void session_stub_set_delay_ms(int ms) {
    atomic_store(&g_stub_delay_ms, ms);
}

int session_process_message(AgentSession* session, const char* user_message) {
    (void)session;
    (void)user_message;

    int delay = atomic_load(&g_stub_delay_ms);
    if (delay > 0) {
        usleep(delay * 1000);
    }

    return atomic_load(&g_stub_return_value);
}
