#include "prompt_mode.h"
#include <mode_prompts.h>
#include <string.h>
#include <ctype.h>

static const char* const MODE_NAMES[] = {
    [PROMPT_MODE_DEFAULT] = "default",
    [PROMPT_MODE_PLAN]    = "plan",
    [PROMPT_MODE_EXPLORE] = "explore",
    [PROMPT_MODE_DEBUG]   = "debug",
    [PROMPT_MODE_REVIEW]  = "review",
};

static const char* const MODE_DESCRIPTIONS[] = {
    [PROMPT_MODE_DEFAULT] = "General-purpose assistant (no behavioral overlay)",
    [PROMPT_MODE_PLAN]    = "Plan and structure before acting",
    [PROMPT_MODE_EXPLORE] = "Read and understand code without modifying",
    [PROMPT_MODE_DEBUG]   = "Diagnose and fix bugs systematically",
    [PROMPT_MODE_REVIEW]  = "Review code for correctness and quality",
};

static const char* const MODE_TEXTS[] = {
    [PROMPT_MODE_DEFAULT] = NULL,
    [PROMPT_MODE_PLAN]    = MODE_PROMPT_PLAN,
    [PROMPT_MODE_EXPLORE] = MODE_PROMPT_EXPLORE,
    [PROMPT_MODE_DEBUG]   = MODE_PROMPT_DEBUG,
    [PROMPT_MODE_REVIEW]  = MODE_PROMPT_REVIEW,
};

const char* prompt_mode_name(PromptMode mode) {
    if (mode < 0 || mode >= PROMPT_MODE_COUNT) return "default";
    return MODE_NAMES[mode];
}

int prompt_mode_from_name(const char* name, PromptMode* out) {
    if (name == NULL || out == NULL) return -1;

    for (int i = 0; i < PROMPT_MODE_COUNT; i++) {
        if (strcasecmp(name, MODE_NAMES[i]) == 0) {
            *out = (PromptMode)i;
            return 0;
        }
    }
    return -1;
}

const char* prompt_mode_get_text(PromptMode mode) {
    if (mode < 0 || mode >= PROMPT_MODE_COUNT) return NULL;
    return MODE_TEXTS[mode];
}

const char* prompt_mode_description(PromptMode mode) {
    if (mode < 0 || mode >= PROMPT_MODE_COUNT) return "";
    return MODE_DESCRIPTIONS[mode];
}
