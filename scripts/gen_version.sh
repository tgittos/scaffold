#!/bin/bash
# Generate build/version.h with version info and git metadata.
# Uses compare-and-swap: only updates the file when content changes,
# so unchanged builds don't trigger recompilation.

set -e

MAJOR="$1"
MINOR="$2"
PATCH="$3"
OUTPUT="$4"

VERSION="${MAJOR}.${MINOR}.${PATCH}"
GIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

if git diff --quiet HEAD 2>/dev/null; then
    GIT_DIRTY=0
    BUILD_VERSION="${VERSION}-${GIT_HASH}"
else
    GIT_DIRTY=1
    BUILD_VERSION="${VERSION}-${GIT_HASH}-dirty"
fi

mkdir -p "$(dirname "$OUTPUT")"

TMPFILE="${OUTPUT}.tmp"
cat > "$TMPFILE" << EOF
#ifndef RALPH_VERSION_H
#define RALPH_VERSION_H

#define RALPH_VERSION_MAJOR ${MAJOR}
#define RALPH_VERSION_MINOR ${MINOR}
#define RALPH_VERSION_PATCH ${PATCH}
#define RALPH_VERSION "${VERSION}"
#define RALPH_GIT_HASH "${GIT_HASH}"
#define RALPH_GIT_DIRTY ${GIT_DIRTY}
#define RALPH_BUILD_VERSION "${BUILD_VERSION}"

#endif
EOF

if [ -f "$OUTPUT" ] && cmp -s "$TMPFILE" "$OUTPUT"; then
    rm "$TMPFILE"
else
    mv "$TMPFILE" "$OUTPUT"
fi
