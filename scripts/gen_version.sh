#!/bin/bash
# Generate build/version.h with version info derived from git tags.
# Uses compare-and-swap: only updates the file when content changes,
# so unchanged builds don't trigger recompilation.
#
# Version is derived from `git describe --tags` (e.g. v0.1.0-14-gabcdef).
# Falls back to 0.0.0 if no tags exist.

set -e

OUTPUT="$1"

if [ -z "$OUTPUT" ]; then
    echo "Usage: $0 <output-file>" >&2
    exit 1
fi

DESC=$(git describe --tags --always --match 'v*' 2>/dev/null || echo "")

if echo "$DESC" | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+'; then
    BASE=$(echo "$DESC" | sed 's/^v//' | sed 's/-.*//')
    MAJOR=$(echo "$BASE" | cut -d. -f1)
    MINOR=$(echo "$BASE" | cut -d. -f2)
    PATCH=$(echo "$BASE" | cut -d. -f3)
else
    MAJOR=0
    MINOR=0
    PATCH=0
fi

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
