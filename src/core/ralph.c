/**
 * src/core/ralph.c - Ralph CLI Entry Point Support
 *
 * This file is now minimal. All session management, payload building,
 * and message processing have been moved to lib/agent/session.c.
 *
 * The Python tool extension is registered via src/tools/python_extension.c
 * which is called from main.c before session initialization.
 */

#include "ralph.h"
