#!/bin/bash
#
# Compact build harness for ralph
# Runs make and outputs results in a compact, AI-readable format
#
# Usage:
#   ./scripts/build.sh           # Build project
#   ./scripts/build.sh -v        # Verbose mode (show all output)
#   ./scripts/build.sh -q        # Quiet mode (errors only)
#   ./scripts/build.sh clean     # Run make clean
#   ./scripts/build.sh target    # Build specific target
#

set -o pipefail

# Colors (disable if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    DIM='\033[2m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' BLUE='' CYAN='' BOLD='' DIM='' NC=''
fi

# Configuration
VERBOSE=0
QUIET=0
TARGET=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -q|--quiet)
            QUIET=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options] [target]"
            echo ""
            echo "Options:"
            echo "  -v, --verbose    Show full make output"
            echo "  -q, --quiet      Errors only (no progress)"
            echo "  -h, --help       Show this help"
            echo ""
            echo "Targets:"
            echo "  (none)           Build all (default)"
            echo "  clean            Remove build artifacts"
            echo "  distclean        Remove everything including deps"
            echo "  test             Build and prepare tests"
            echo "  <target>         Build specific make target"
            exit 0
            ;;
        *)
            TARGET="$1"
            shift
            ;;
    esac
done

# Format duration
format_duration() {
    local ms="$1"
    if [ "$ms" -lt 1000 ]; then
        echo "${ms}ms"
    elif [ "$ms" -lt 60000 ]; then
        echo "$((ms / 1000)).$((ms % 1000 / 100))s"
    else
        local secs=$((ms / 1000))
        echo "$((secs / 60))m$((secs % 60))s"
    fi
}

# Extract errors from make output
extract_errors() {
    local output="$1"
    # Match GCC/Clang error patterns
    echo "$output" | grep -E '(error:|Error:|ERROR:|undefined reference|multiple definition|fatal error:)' | head -20
}

# Extract warnings from make output
extract_warnings() {
    local output="$1"
    echo "$output" | grep -E '(warning:|Warning:|WARNING:)' | head -10
}

# Count compiled files
count_compiled() {
    local output="$1"
    local count
    count=$(echo "$output" | grep -cE '^\s*(CC|LD|AR|COSMOCC)\s' 2>/dev/null) || count=0
    echo "${count:-0}"
}

# Main execution
main() {
    local target_display="${TARGET:-all}"

    if [ $QUIET -eq 0 ]; then
        echo ""
        echo "${BOLD}ralph build${NC}"
        echo "${DIM}$(date '+%Y-%m-%d %H:%M:%S')${NC}"
        echo ""
        echo "${DIM}Target: ${target_display}${NC}"
        echo ""
    fi

    local start_time=$(date +%s)

    # Run make
    cd "$PROJECT_DIR"
    local output
    local exit_code

    if [ $VERBOSE -eq 1 ]; then
        # Verbose: show output in real-time
        make $TARGET 2>&1 | tee /tmp/ralph_build_$$.log
        exit_code=${PIPESTATUS[0]}
        output=$(cat /tmp/ralph_build_$$.log)
        rm -f /tmp/ralph_build_$$.log
    else
        # Capture output
        output=$(make $TARGET 2>&1)
        exit_code=$?
    fi

    local end_time=$(date +%s)
    local duration=$(( (end_time - start_time) * 1000 ))

    # Extract metrics
    local compiled=$(count_compiled "$output")
    local errors=$(extract_errors "$output")
    local warnings=$(extract_warnings "$output")
    local error_count=0
    local warning_count=0
    if [ -n "$errors" ]; then
        error_count=$(echo "$errors" | wc -l | tr -d ' ')
    fi
    if [ -n "$warnings" ]; then
        warning_count=$(echo "$warnings" | wc -l | tr -d ' ')
    fi

    # Handle "nothing to be done" case
    local nothing_to_do=0
    if echo "$output" | grep -q "Nothing to be done\|is up to date"; then
        nothing_to_do=1
    fi

    # Print results
    if [ $exit_code -eq 0 ]; then
        if [ $QUIET -eq 0 ]; then
            echo "${GREEN}${BOLD}BUILD SUCCESSFUL${NC}"
            echo ""
            if [ $nothing_to_do -eq 1 ]; then
                echo "${DIM}Nothing to rebuild (already up to date)${NC}"
            elif [ "$compiled" -gt 0 ]; then
                echo "${DIM}Compiled $compiled file(s)${NC}"
            fi
            if [ "$warning_count" -gt 0 ]; then
                echo "${YELLOW}Warnings: $warning_count${NC}"
                if [ $VERBOSE -eq 0 ]; then
                    echo ""
                    echo "${YELLOW}$warnings${NC}" | head -5
                    if [ "$warning_count" -gt 5 ]; then
                        echo "${DIM}... and $((warning_count - 5)) more (use -v to see all)${NC}"
                    fi
                fi
            fi
            echo "${DIM}Duration: $(format_duration $duration)${NC}"
        fi
    else
        echo "${RED}${BOLD}BUILD FAILED${NC}"
        echo ""

        if [ -n "$errors" ]; then
            echo "${RED}Errors:${NC}"
            echo "$errors" | while IFS= read -r line; do
                # Simplify path for readability
                local simplified=$(echo "$line" | sed "s|$PROJECT_DIR/||g")
                echo "  ${RED}$simplified${NC}"
            done
            echo ""
        fi

        if [ "$warning_count" -gt 0 ] && [ $QUIET -eq 0 ]; then
            echo "${YELLOW}Warnings: $warning_count${NC}"
            echo ""
        fi

        # Show last lines of output for context if no clear errors found
        if [ -z "$errors" ]; then
            echo "${DIM}Last output:${NC}"
            echo "$output" | tail -15 | sed 's/^/  /'
            echo ""
        fi

        echo "${DIM}Duration: $(format_duration $duration)${NC}"
        echo ""
        echo "${DIM}Run with -v for full output${NC}"
    fi

    echo ""
    exit $exit_code
}

main
