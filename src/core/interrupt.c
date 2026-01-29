#include "interrupt.h"
#include <signal.h>
#include <string.h>

static volatile sig_atomic_t g_interrupt_flag = 0;
static volatile sig_atomic_t g_acknowledging = 0;
static struct sigaction g_old_action;
static int g_handler_installed = 0;

static void interrupt_handler(int sig) {
    (void)sig;
    g_interrupt_flag = 1;
}

int interrupt_init(void) {
    if (g_handler_installed) {
        return 0;
    }

    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler = interrupt_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    if (sigaction(SIGINT, &new_action, &g_old_action) != 0) {
        return -1;
    }

    g_handler_installed = 1;
    g_interrupt_flag = 0;
    g_acknowledging = 0;

    return 0;
}

void interrupt_cleanup(void) {
    if (!g_handler_installed) {
        return;
    }

    sigaction(SIGINT, &g_old_action, NULL);
    g_handler_installed = 0;
    g_interrupt_flag = 0;
    g_acknowledging = 0;
}

int interrupt_pending(void) {
    return g_interrupt_flag != 0 && g_acknowledging == 0;
}

void interrupt_clear(void) {
    g_interrupt_flag = 0;
    g_acknowledging = 0;
}

void interrupt_acknowledge(void) {
    g_acknowledging = 1;
}

void interrupt_handler_trigger(void) {
    g_interrupt_flag = 1;
}
