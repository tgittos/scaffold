/**
 * lib/ui/spinner.h - Library wrapper for spinner
 *
 * This header re-exports the spinner implementation from src/
 * through the library API. It provides compatibility between the
 * internal src/ interface and the public lib/ interface.
 *
 * Source implementation: src/utils/spinner.c
 */

#ifndef LIB_UI_SPINNER_H
#define LIB_UI_SPINNER_H

/* Re-export the original implementation */
#include "../../src/utils/spinner.h"

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

#define ralph_spinner_start    spinner_start
#define ralph_spinner_stop     spinner_stop
#define ralph_spinner_cleanup  spinner_cleanup

#endif /* LIB_UI_SPINNER_H */
