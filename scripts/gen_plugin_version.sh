#!/bin/bash
# Generate build/plugin_<name>_version.h with version info derived from plugin git tags.
# Uses compare-and-swap: only updates the file when content changes,
# so unchanged builds don't trigger recompilation.
#
# Version is derived from `git describe --tags --match 'plugin-<name>-v*'`.
# Falls back to 0.0.0 if no matching tags exist.
#
# Usage: gen_plugin_version.sh <plugin-name> <output-file>

set -e

NAME="$1"
OUTPUT="$2"

if [ -z "$NAME" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: $0 <plugin-name> <output-file>" >&2
    exit 1
fi

DESC=$(git describe --tags --always --match "plugin-${NAME}-v*" 2>/dev/null || echo "")

if echo "$DESC" | grep -qE "^plugin-${NAME}-v[0-9]+\.[0-9]+\.[0-9]+"; then
    BASE=$(echo "$DESC" | sed "s/^plugin-${NAME}-v//" | sed 's/-.*//')
    MAJOR=$(echo "$BASE" | cut -d. -f1)
    MINOR=$(echo "$BASE" | cut -d. -f2)
    PATCH=$(echo "$BASE" | cut -d. -f3)
else
    MAJOR=0
    MINOR=0
    PATCH=0
fi

VERSION="${MAJOR}.${MINOR}.${PATCH}"

# Convert plugin name to C-safe identifier (hyphens -> underscores)
SAFE_NAME=$(echo "$NAME" | tr '-' '_')
GUARD="PLUGIN_${SAFE_NAME}_VERSION_H"
GUARD=$(echo "$GUARD" | tr '[:lower:]' '[:upper:]')

mkdir -p "$(dirname "$OUTPUT")"

TMPFILE="${OUTPUT}.tmp"
cat > "$TMPFILE" << EOF
#ifndef ${GUARD}
#define ${GUARD}

#define PLUGIN_VERSION_MAJOR ${MAJOR}
#define PLUGIN_VERSION_MINOR ${MINOR}
#define PLUGIN_VERSION_PATCH ${PATCH}
#define PLUGIN_VERSION "${VERSION}"

#endif
EOF

if [ -f "$OUTPUT" ] && cmp -s "$TMPFILE" "$OUTPUT"; then
    rm "$TMPFILE"
else
    mv "$TMPFILE" "$OUTPUT"
fi
