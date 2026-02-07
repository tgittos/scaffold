#!/bin/bash
# Bump version in mk/config.mk, commit, and create a git tag.
# Usage: scripts/bump_version.sh [major|minor|patch]

set -e

CONFIG_FILE="mk/config.mk"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: $CONFIG_FILE not found (run from project root)" >&2
    exit 1
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" != "master" ]; then
    echo "Error: Must be on master branch to bump version (currently on '${BRANCH}')" >&2
    exit 1
fi

MAJOR=$(grep '^RALPH_VERSION_MAJOR' "$CONFIG_FILE" | awk '{print $NF}')
MINOR=$(grep '^RALPH_VERSION_MINOR' "$CONFIG_FILE" | awk '{print $NF}')
PATCH=$(grep '^RALPH_VERSION_PATCH' "$CONFIG_FILE" | awk '{print $NF}')
CURRENT="${MAJOR}.${MINOR}.${PATCH}"

case "${1:-patch}" in
    major)
        MAJOR=$((MAJOR + 1))
        MINOR=0
        PATCH=0
        ;;
    minor)
        MINOR=$((MINOR + 1))
        PATCH=0
        ;;
    patch)
        PATCH=$((PATCH + 1))
        ;;
    *)
        echo "Usage: $0 [major|minor|patch]" >&2
        exit 1
        ;;
esac

NEW="${MAJOR}.${MINOR}.${PATCH}"

echo "Bumping version: ${CURRENT} -> ${NEW}"

sed -i "s/^RALPH_VERSION_MAJOR := .*/RALPH_VERSION_MAJOR := ${MAJOR}/" "$CONFIG_FILE"
sed -i "s/^RALPH_VERSION_MINOR := .*/RALPH_VERSION_MINOR := ${MINOR}/" "$CONFIG_FILE"
sed -i "s/^RALPH_VERSION_PATCH := .*/RALPH_VERSION_PATCH := ${PATCH}/" "$CONFIG_FILE"

echo "Updated ${CONFIG_FILE}"

git add "$CONFIG_FILE"
git commit -m "chore: Bump version to ${NEW}"
git tag -a "v${NEW}" -m "Release v${NEW}"

echo ""
echo "Created tag v${NEW}"
echo "To release: git push origin master --tags"
