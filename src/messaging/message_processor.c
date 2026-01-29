#include "message_processor.h"
#include "message_poller.h"
#include "notification_formatter.h"
#include "../utils/output_formatter.h"
#include "../utils/debug_output.h"
#include <stdlib.h>

int process_incoming_messages(RalphSession* session, message_poller_t* poller) {
    if (session == NULL || poller == NULL) {
        return -1;
    }

    message_poller_clear_notification(poller);

    const char* agent_id = session->session_id;
    notification_bundle_t* bundle = notification_bundle_create(agent_id);
    if (bundle == NULL) {
        return -1;
    }

    int total_count = notification_bundle_total_count(bundle);
    if (total_count == 0) {
        notification_bundle_destroy(bundle);
        return 0;
    }

    display_message_notification(total_count);

    char* notification_text = notification_format_for_llm(bundle);
    notification_bundle_destroy(bundle);

    if (notification_text == NULL) {
        display_message_notification_clear();
        return -1;
    }

    debug_printf("Processing %d incoming messages\n", total_count);

    int result = ralph_process_message(session, notification_text);

    free(notification_text);

    return result;
}
