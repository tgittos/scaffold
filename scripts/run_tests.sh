#!/bin/bash
#
# Compact test harness for ralph
# Runs all tests and outputs results in a compact, readable format
#
# Usage:
#   ./scripts/run_tests.sh           # Run all tests
#   ./scripts/run_tests.sh -v        # Verbose mode (show all output)
#   ./scripts/run_tests.sh -f        # Show only failures
#   ./scripts/run_tests.sh -q        # Quiet mode (summary only)
#   ./scripts/run_tests.sh test_foo  # Run specific test(s) matching pattern
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
FAILURES_ONLY=0
PATTERN=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$PROJECT_DIR/test"

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
        -f|--failures)
            FAILURES_ONLY=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options] [pattern]"
            echo ""
            echo "Options:"
            echo "  -v, --verbose    Show full test output"
            echo "  -q, --quiet      Summary only (no per-test output)"
            echo "  -f, --failures   Show only failed tests"
            echo "  -h, --help       Show this help"
            echo ""
            echo "Pattern:"
            echo "  Optional pattern to filter tests (e.g., 'darray' runs test_darray)"
            exit 0
            ;;
        *)
            PATTERN="$1"
            shift
            ;;
    esac
done

# Find all test binaries
find_tests() {
    local tests=()

    # Use make to print the actual test paths
    local make_output
    make_output=$(make -C "$PROJECT_DIR" -n test 2>/dev/null | grep -oE '\./test/test_[a-zA-Z0-9_]+' | sed 's|^\./||' | uniq)

    if [ -n "$make_output" ]; then
        while IFS= read -r test_path; do
            if [ -x "$PROJECT_DIR/$test_path" ]; then
                tests+=("$test_path")
            fi
        done <<< "$make_output"
    fi

    # Fallback: find test binaries directly
    if [ ${#tests[@]} -eq 0 ]; then
        while IFS= read -r -d '' test; do
            local basename=$(basename "$test")
            if [[ "$basename" == test_* ]] && [[ ! "$basename" =~ \. ]]; then
                tests+=("${test#$PROJECT_DIR/}")
            fi
        done < <(find "$TEST_DIR" -maxdepth 2 -type f -executable -name "test_*" ! -name "*.o" ! -name "*.elf" ! -name "*.dbg" -print0 2>/dev/null | sort -z)
    fi

    # Filter by pattern if provided
    if [ -n "$PATTERN" ]; then
        local filtered=()
        for t in "${tests[@]}"; do
            if [[ "$t" == *"$PATTERN"* ]]; then
                filtered+=("$t")
            fi
        done
        tests=("${filtered[@]}")
    fi

    printf '%s\n' "${tests[@]}"
}

# Parse Unity output and extract summary
parse_unity_output() {
    local output="$1"
    local tests=0 failures=0 ignored=0

    # Extract summary line: "X Tests Y Failures Z Ignored"
    if [[ "$output" =~ ([0-9]+)\ Tests\ ([0-9]+)\ Failures\ ([0-9]+)\ Ignored ]]; then
        tests="${BASH_REMATCH[1]}"
        failures="${BASH_REMATCH[2]}"
        ignored="${BASH_REMATCH[3]}"
    fi

    echo "$tests $failures $ignored"
}

# Extract failed test names from Unity output
extract_failures() {
    local output="$1"
    echo "$output" | grep -E ':FAIL' | sed 's/.*:\([^:]*\):FAIL.*/  \1/'
}

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

# Print a progress indicator
print_progress() {
    local current="$1"
    local total="$2"
    local name="$3"
    local status="$4"  # running, pass, fail
    local extra="$5"   # extra info (timing, counts)

    if [ $QUIET -eq 1 ]; then
        return
    fi

    # Clear line escape sequence
    local CLEAR='\033[2K'

    case "$status" in
        running)
            printf "\r${CLEAR}${DIM}[%d/%d]${NC} ${CYAN}%s${NC}..." "$current" "$total" "$name"
            ;;
        pass)
            printf "\r${CLEAR}${GREEN}✓${NC} %-25s ${DIM}%s${NC}\n" "$name" "$extra"
            ;;
        fail)
            printf "\r${CLEAR}${RED}✗${NC} %-25s ${DIM}%s${NC}\n" "$name" "$extra"
            ;;
    esac
}

# Run a single test
run_test() {
    local test_path="$1"
    local test_name=$(basename "$test_path")
    local start_time=$(date +%s%3N 2>/dev/null || date +%s)000

    # Run test and capture output
    local output
    local exit_code

    cd "$PROJECT_DIR"
    output=$(./"$test_path" 2>&1)
    exit_code=$?

    local end_time=$(date +%s%3N 2>/dev/null || date +%s)000
    local duration=$((end_time - start_time))

    # Parse results
    local summary=$(parse_unity_output "$output")
    local tests=$(echo "$summary" | cut -d' ' -f1)
    local failures=$(echo "$summary" | cut -d' ' -f2)
    local ignored=$(echo "$summary" | cut -d' ' -f3)

    # Handle test that didn't produce Unity output
    if [ -z "$tests" ] || [ "$tests" -eq 0 ]; then
        if [ $exit_code -eq 0 ]; then
            tests=1
            failures=0
        else
            tests=1
            failures=1
        fi
    fi

    # Detect segfault
    local segfault=0
    if echo "$output" | grep -qi "segmentation fault\|segfault\|SIGSEGV"; then
        segfault=1
    fi

    # Determine pass/fail
    local passed=1
    if [ $exit_code -ne 0 ] || [ "$failures" -gt 0 ] || [ $segfault -eq 1 ]; then
        passed=0
    fi

    # Output result
    echo "$test_name|$passed|$tests|$failures|$ignored|$duration|$segfault|$output"
}

# Main execution
main() {
    echo ""
    echo "${BOLD}ralph test suite${NC}"
    echo "${DIM}$(date '+%Y-%m-%d %H:%M:%S')${NC}"
    echo ""

    # Find tests
    mapfile -t tests < <(find_tests)
    local total=${#tests[@]}

    if [ $total -eq 0 ]; then
        echo "${YELLOW}No tests found${NC}"
        if [ -n "$PATTERN" ]; then
            echo "Pattern '$PATTERN' matched no tests"
        fi
        exit 1
    fi

    echo "${DIM}Running $total test(s)...${NC}"
    echo ""

    # Track results
    local passed=0
    local failed=0
    local segfaults=0
    local total_tests=0
    local total_failures=0
    local total_ignored=0
    local failed_tests=()
    local segfault_tests=()
    local failure_details=()
    local start_time=$(date +%s%3N 2>/dev/null || date +%s)000

    # Run each test
    local current=0
    for test_path in "${tests[@]}"; do
        current=$((current + 1))
        local test_name=$(basename "$test_path")

        # Show progress
        print_progress $current $total "$test_name" "running"

        # Run test
        local result=$(run_test "$test_path")

        # Parse result
        IFS='|' read -r name is_passed tests failures ignored duration segfault output <<< "$result"

        total_tests=$((total_tests + tests))
        total_failures=$((total_failures + failures))
        total_ignored=$((total_ignored + ignored))

        if [ "$is_passed" -eq 1 ]; then
            passed=$((passed + 1))
            if [ $FAILURES_ONLY -eq 0 ]; then
                print_progress $current $total "$name" "pass" "${tests} tests, $(format_duration $duration)"
                if [ $VERBOSE -eq 1 ]; then
                    echo "${DIM}$output${NC}" | sed 's/^/    /'
                    echo ""
                fi
            fi
        else
            failed=$((failed + 1))
            failed_tests+=("$name")
            failure_details+=("$output")

            # Check for segfault - this is a critical failure
            if [ "$segfault" -eq 1 ]; then
                segfaults=$((segfaults + 1))
                segfault_tests+=("$name")
                printf "\r\033[2K${RED}${BOLD}💥 SEGFAULT${NC} %-25s\n" "$name"
                echo ""
                echo "${RED}${BOLD}   ╔══════════════════════════════════════════════════════════╗${NC}"
                echo "${RED}${BOLD}   ║  SEGMENTATION FAULT DETECTED - CRITICAL MEMORY ERROR    ║${NC}"
                echo "${RED}${BOLD}   ╚══════════════════════════════════════════════════════════╝${NC}"
                echo ""
                echo "${YELLOW}   Run with valgrind for details:${NC}"
                echo "${DIM}   valgrind --leak-check=full ./$test_path.aarch64.elf${NC}"
                echo ""
                # Show last few lines of output
                echo "${DIM}   Last output:${NC}"
                echo "$output" | tail -10 | sed 's/^/   /'
                echo ""
            else
                print_progress $current $total "$name" "fail" "${failures}/${tests} failed"

                if [ $VERBOSE -eq 1 ]; then
                    echo "${DIM}$output${NC}" | sed 's/^/    /'
                    echo ""
                else
                    # Show just the failure lines
                    local fail_lines=$(extract_failures "$output")
                    if [ -n "$fail_lines" ]; then
                        echo "${DIM}$fail_lines${NC}"
                    fi
                fi
            fi
        fi
    done

    local end_time=$(date +%s%3N 2>/dev/null || date +%s)000
    local total_duration=$((end_time - start_time))

    # Print summary
    echo ""
    echo "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""

    if [ $segfaults -gt 0 ]; then
        echo "${RED}${BOLD}💥 SEGMENTATION FAULTS DETECTED${NC}"
    elif [ $failed -eq 0 ]; then
        echo "${GREEN}${BOLD}ALL TESTS PASSED${NC}"
    else
        echo "${RED}${BOLD}TESTS FAILED${NC}"
    fi

    echo ""
    printf "${BOLD}Binaries:${NC}  %d passed, %d failed" $passed $failed
    if [ $segfaults -gt 0 ]; then
        printf " ${RED}(%d segfaults)${NC}" $segfaults
    fi
    printf ", %d total\n" $total
    printf "${BOLD}Tests:${NC}     %d passed, %d failed, %d ignored, %d total\n" \
        $((total_tests - total_failures - total_ignored)) $total_failures $total_ignored $total_tests
    printf "${BOLD}Duration:${NC}  %s\n" "$(format_duration $total_duration)"

    # Show segfaults first (most critical)
    if [ $segfaults -gt 0 ]; then
        echo ""
        echo "${RED}${BOLD}💥 Segfaulting tests (CRITICAL):${NC}"
        for name in "${segfault_tests[@]}"; do
            echo "  ${RED}💥${NC} $name"
        done
    fi

    # Show other failed tests
    local other_failures=()
    for name in "${failed_tests[@]}"; do
        local is_segfault=0
        for sf in "${segfault_tests[@]}"; do
            if [ "$name" = "$sf" ]; then
                is_segfault=1
                break
            fi
        done
        if [ $is_segfault -eq 0 ]; then
            other_failures+=("$name")
        fi
    done

    if [ ${#other_failures[@]} -gt 0 ]; then
        echo ""
        echo "${RED}${BOLD}Failed tests:${NC}"
        for name in "${other_failures[@]}"; do
            echo "  ${RED}✗${NC} $name"
        done
    fi

    echo ""

    # Exit with appropriate code (segfaults exit with 2 to distinguish)
    if [ $segfaults -gt 0 ]; then
        exit 2
    fi
    [ $failed -eq 0 ]
}

main
