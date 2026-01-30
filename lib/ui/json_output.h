/**
 * lib/ui/json_output.h - Library wrapper for JSON output
 *
 * This header re-exports the JSON output implementation from src/
 * through the library API. It provides compatibility between the
 * internal src/ interface and the public lib/ interface.
 *
 * Source implementation: src/utils/json_output.c
 */

#ifndef LIB_UI_JSON_OUTPUT_H
#define LIB_UI_JSON_OUTPUT_H

/* Re-export the original implementation */
#include "../../src/utils/json_output.h"

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

#define ralph_json_output_init             json_output_init
#define ralph_json_output_assistant_text   json_output_assistant_text
#define ralph_json_output_tool_result      json_output_tool_result
#define ralph_json_output_system           json_output_system
#define ralph_json_output_error            json_output_error
#define ralph_json_output_result           json_output_result

#endif /* LIB_UI_JSON_OUTPUT_H */
