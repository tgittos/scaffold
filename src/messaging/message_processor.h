#ifndef MESSAGE_PROCESSOR_H
#define MESSAGE_PROCESSOR_H

#include "../core/ralph.h"
#include "message_poller.h"

int process_incoming_messages(RalphSession* session, message_poller_t* poller);

#endif
