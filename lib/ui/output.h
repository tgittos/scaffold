/**
 * lib/ui/output.h - Library wrapper for output formatting
 *
 * This header re-exports the output formatting implementation
 * through the library API.
 *
 * Source implementation: lib/ui/output_formatter.c
 */

#ifndef LIB_UI_OUTPUT_H
#define LIB_UI_OUTPUT_H

/* Re-export the implementation */
#include "output_formatter.h"

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

/* Output mode configuration */
#define ralph_output_set_json_mode    set_json_output_mode
#define ralph_output_get_json_mode    get_json_output_mode

/* Streaming display */
#define ralph_output_streaming_init      display_streaming_init
#define ralph_output_streaming_text      display_streaming_text
#define ralph_output_streaming_thinking  display_streaming_thinking
#define ralph_output_streaming_tool_start  display_streaming_tool_start
#define ralph_output_streaming_complete  display_streaming_complete
#define ralph_output_streaming_error     display_streaming_error

/* System info display */
#define ralph_output_system_info_start   display_system_info_group_start
#define ralph_output_system_info_end     display_system_info_group_end
#define ralph_output_system_info         log_system_info

/* Tool execution logging */
#define ralph_output_tool_execution      log_tool_execution_improved

/* Response formatting */
#define ralph_output_response            print_formatted_response_improved

/* Cleanup */
#define ralph_output_cleanup             cleanup_output_formatter

#endif /* LIB_UI_OUTPUT_H */
