#!/bin/bash
# Generate build/generated/ headers from data/ files.
# Uses compare-and-swap: only updates files when content changes,
# so unchanged builds don't trigger recompilation.

set -e

OUTDIR="${1:?Usage: gen_data.sh <output-dir>}"
DATADIR="data"

mkdir -p "$OUTDIR"

# ---------- helper: compare-and-swap ----------
swap_if_changed() {
    local tmp="$1" final="$2"
    if [ -f "$final" ] && cmp -s "$tmp" "$final"; then
        rm "$tmp"
    else
        mv "$tmp" "$final"
    fi
}

# ---------- helper: escape C string ----------
# Reads stdin, outputs a C string literal body (no surrounding quotes).
# Handles: backslash, double-quote, newline, tab, carriage return.
c_escape() {
    sed -e 's/\\/\\\\/g' \
        -e 's/"/\\"/g' \
        -e 's/	/\\t/g' \
        -e 's/\r/\\r/g' \
        -e 's/$/\\n/' | \
    tr -d '\n'
}

# ---------- helper: escape a single-line string for C ----------
# Like c_escape but for values that are already single-line (no newline append).
c_escape_value() {
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

# =============================================================================
# 1. prompt_data.h
# =============================================================================

PROMPT1="$DATADIR/prompts/system.txt"
PROMPT_SCAFFOLD="$DATADIR/prompts/scaffold_system.txt"
TMP="$OUTDIR/prompt_data.h.tmp"

ESCAPED1=$(c_escape < "$PROMPT1")
ESCAPED_SCAFFOLD=$(c_escape < "$PROMPT_SCAFFOLD")

{
    cat << 'HEOF'
#ifndef PROMPT_DATA_H
#define PROMPT_DATA_H

HEOF
    # Use printf with %s to avoid shell interpretation of escaped content
    printf 'static const char SYSTEM_PROMPT_TEXT[] = "%s";\n\n' "$ESCAPED1"
    printf 'static const char SCAFFOLD_SYSTEM_PROMPT_TEXT[] = "%s";\n' "$ESCAPED_SCAFFOLD"
    cat << 'HEOF'

#endif
HEOF
} > "$TMP"

swap_if_changed "$TMP" "$OUTDIR/prompt_data.h"

# =============================================================================
# 2. defaults.h
# =============================================================================

DEFAULTS="$DATADIR/defaults.json"
TMP="$OUTDIR/defaults.h.tmp"

api_url=$(jq -r '.api_url' "$DEFAULTS" | c_escape_value)
model=$(jq -r '.model' "$DEFAULTS" | c_escape_value)
context_window=$(jq -r '.context_window' "$DEFAULTS")
max_tokens=$(jq -r '.max_tokens' "$DEFAULTS")
api_max_retries=$(jq -r '.api_max_retries' "$DEFAULTS")
api_retry_delay_ms=$(jq -r '.api_retry_delay_ms' "$DEFAULTS")
api_backoff_factor=$(jq -r '.api_backoff_factor | tostring | if test("\\.") then . else . + ".0" end' "$DEFAULTS")
max_subagents=$(jq -r '.max_subagents' "$DEFAULTS")
subagent_timeout=$(jq -r '.subagent_timeout' "$DEFAULTS")
enable_streaming=$(jq -r '.enable_streaming' "$DEFAULTS")
json_output_mode=$(jq -r '.json_output_mode' "$DEFAULTS")
check_updates=$(jq -r '.check_updates' "$DEFAULTS")
model_simple=$(jq -r '.tiers.simple' "$DEFAULTS" | c_escape_value)
model_standard=$(jq -r '.tiers.standard' "$DEFAULTS" | c_escape_value)
model_high=$(jq -r '.tiers.high' "$DEFAULTS" | c_escape_value)

bool_to_c() { [ "$1" = "true" ] && echo "1" || echo "0"; }

cat > "$TMP" << HEOF
#ifndef DEFAULTS_DATA_H
#define DEFAULTS_DATA_H

#define DEFAULT_API_URL "${api_url}"
#define DEFAULT_MODEL "${model}"
#define DEFAULT_CONTEXT_WINDOW ${context_window}
#define DEFAULT_MAX_TOKENS (${max_tokens})
#define DEFAULT_API_MAX_RETRIES ${api_max_retries}
#define DEFAULT_API_RETRY_DELAY_MS ${api_retry_delay_ms}
#define DEFAULT_API_BACKOFF_FACTOR ${api_backoff_factor}f
#define DEFAULT_MAX_SUBAGENTS ${max_subagents}
#define DEFAULT_SUBAGENT_TIMEOUT ${subagent_timeout}
#define DEFAULT_ENABLE_STREAMING $(bool_to_c "$enable_streaming")
#define DEFAULT_JSON_OUTPUT_MODE $(bool_to_c "$json_output_mode")
#define DEFAULT_CHECK_UPDATES $(bool_to_c "$check_updates")
#define DEFAULT_MODEL_SIMPLE "${model_simple}"
#define DEFAULT_MODEL_STANDARD "${model_standard}"
#define DEFAULT_MODEL_HIGH "${model_high}"

#endif
HEOF

swap_if_changed "$TMP" "$OUTDIR/defaults.h"

# =============================================================================
# 3. mode_prompts.h
# =============================================================================

MODES_DIR="$DATADIR/prompts/modes"
TMP="$OUTDIR/mode_prompts.h.tmp"

{
    cat << 'HEOF'
#ifndef MODE_PROMPTS_H
#define MODE_PROMPTS_H

HEOF

    for mode in plan explore debug review; do
        MODE_FILE="$MODES_DIR/${mode}.txt"
        if [ -f "$MODE_FILE" ]; then
            UPPER=$(echo "$mode" | tr 'a-z' 'A-Z')
            ESCAPED=$(c_escape < "$MODE_FILE")
            printf 'static const char MODE_PROMPT_%s[] = "%s";\n\n' "$UPPER" "$ESCAPED"
        fi
    done

    cat << 'HEOF'
#endif
HEOF
} > "$TMP"

swap_if_changed "$TMP" "$OUTDIR/mode_prompts.h"
