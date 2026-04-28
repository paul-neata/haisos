#!/bin/bash

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

PLATFORM_ARG="${1:-L}"
SELECTOR_ARG="${2:-*}"
shift 2 || true

CONFIGS="release"
FILTER=""
while [[ $# -gt 0 ]]; do
    arg="$1"
    if [[ "$arg" == "--debug" ]]; then
        CONFIGS="debug"
        shift
    elif [[ "$arg" == "--both" ]]; then
        CONFIGS="both"
        shift
    else
        FILTER="$arg"
        shift
        while [[ $# -gt 0 ]]; do
            FILTER="$FILTER $1"
            shift
        done
    fi
done

PLATFORMS=""
PLATFORM_ARG_UPPER=$(echo "$PLATFORM_ARG" | tr '[:lower:]' '[:upper:]')
if [[ "$PLATFORM_ARG_UPPER" == "*" ]]; then
    PLATFORMS="L W N"
else
    [[ "$PLATFORM_ARG_UPPER" == *"L"* ]] && PLATFORMS="$PLATFORMS L"
    [[ "$PLATFORM_ARG_UPPER" == *"W"* ]] && PLATFORMS="$PLATFORMS W"
    [[ "$PLATFORM_ARG_UPPER" == *"N"* ]] && PLATFORMS="$PLATFORMS N"
fi

TESTS=""
SELECTOR_ARG_UPPER=$(echo "$SELECTOR_ARG" | tr '[:lower:]' '[:upper:]')
if [[ "$SELECTOR_ARG_UPPER" == "*" ]]; then
    TESTS="U I H"
else
    [[ "$SELECTOR_ARG_UPPER" == *"U"* ]] && TESTS="$TESTS U"
    [[ "$SELECTOR_ARG_UPPER" == *"I"* ]] && TESTS="$TESTS I"
    [[ "$SELECTOR_ARG_UPPER" == *"H"* ]] && TESTS="$TESTS H"
fi

CONFIG_LIST=()
if [[ "$CONFIGS" == "both" ]]; then
    CONFIG_LIST=("release" "debug")
elif [[ "$CONFIGS" == "debug" ]]; then
    CONFIG_LIST=("debug")
else
    CONFIG_LIST=("release")
fi

IS_WSL=0
if grep -qi microsoft /proc/version 2>/dev/null; then
    IS_WSL=1
fi

ANY_FAILED=0

run_tests_for_platform() {
    local platform="$1"
    local config="$2"

    local build_suffix=""
    if [[ "$config" == "debug" ]]; then
        build_suffix="_debug"
    fi

    local output_dir=""
    case "$platform" in
        L)
            output_dir="$PROJECT_DIR/output/linux${build_suffix}"
            ;;
        W)
            output_dir="$PROJECT_DIR/output/windows${build_suffix}"
            ;;
        N)
            output_dir="$PROJECT_DIR/output/wasm${build_suffix}"
            ;;
    esac

    local unit_passed=0
    local unit_total=0
    local unit_failed_names=()

    local int_passed=0
    local int_total=0
    local int_failed_names=()

    local haisos_passed=0
    local haisos_total=0
    local haisos_failed_names=()

    # Unit tests
    if [[ "$TESTS" == *"U"* ]]; then
        local unit_pattern=""
        case "$platform" in
            L) unit_pattern="$output_dir"/*.unittests ;;
            W) unit_pattern="$output_dir"/*.unittests.exe ;;
            N) unit_pattern="$output_dir"/*.unittests.js ;;
        esac

        for test in $unit_pattern; do
            [[ -e "$test" ]] || continue
            [[ -f "$test" ]] || continue

            local name
            name=$(basename "$test")
            if [[ -n "$FILTER" && "$name" != *"$FILTER"* ]]; then
                continue
            fi

            unit_total=$((unit_total + 1))
            echo ""
            echo "Running: $test"
            local gtest_args=()
            if [[ -n "$FILTER" ]]; then
                gtest_args+=("--gtest_filter=*${FILTER}*")
            fi

            if "$test" "${gtest_args[@]}"; then
                unit_passed=$((unit_passed + 1))
            else
                unit_failed_names+=("$name")
                ANY_FAILED=1
            fi
        done
    fi

    # Integration tests
    if [[ "$TESTS" == *"I"* ]]; then
        local int_pattern=""
        case "$platform" in
            L) int_pattern="$output_dir"/*.integrationtest ;;
            W) int_pattern="$output_dir"/*.integrationtest.exe ;;
            N) int_pattern="$output_dir"/*.integrationtest.js ;;
        esac

        for test in $int_pattern; do
            [[ -e "$test" ]] || continue
            [[ -f "$test" ]] || continue

            local name
            name=$(basename "$test")
            if [[ -n "$FILTER" && "$name" != *"$FILTER"* ]]; then
                continue
            fi

            int_total=$((int_total + 1))

            if "$test"; then
                int_passed=$((int_passed + 1))
            else
                int_failed_names+=("$name")
                ANY_FAILED=1
            fi
        done
    fi

    # Haisos tests
    if [[ "$TESTS" == *"H"* ]]; then
        for test in "$PROJECT_DIR"/tests/haisos/*.haisostest/*.haisostest.js; do
            [[ -f "$test" ]] || continue

            local name
            name=$(basename "$test")
            if [[ -n "$FILTER" && "$name" != *"$FILTER"* ]]; then
                continue
            fi

            haisos_total=$((haisos_total + 1))
            echo ""
            echo "Running: node $test"
            if node "$test"; then
                haisos_passed=$((haisos_passed + 1))
            else
                haisos_failed_names+=("$name")
                ANY_FAILED=1
            fi
        done
    fi

    local platform_name=""
    case "$platform" in
        L) platform_name="Linux" ;;
        W) platform_name="Windows" ;;
        N) platform_name="WASM" ;;
    esac

    echo ""
    echo "========================================"
    echo "$platform_name $config tests"
    echo "========================================"

    if [[ "$TESTS" == *"U"* ]]; then
        local unit_summary="$unit_passed/$unit_total unit tests pass"
        if [[ ${#unit_failed_names[@]} -gt 0 ]]; then
            unit_summary="$unit_summary | FAILED: ${unit_failed_names[*]}"
        fi
        echo "$unit_summary"
    else
        echo "-/- unit tests pass"
    fi

    if [[ "$TESTS" == *"I"* ]]; then
        local int_summary="$int_passed/$int_total integration tests pass"
        if [[ ${#int_failed_names[@]} -gt 0 ]]; then
            int_summary="$int_summary | FAILED: ${int_failed_names[*]}"
        fi
        echo "$int_summary"
    else
        echo "-/- integration tests pass"
    fi

    if [[ "$TESTS" == *"H"* ]]; then
        local haisos_summary="$haisos_passed/$haisos_total haisos tests pass"
        if [[ ${#haisos_failed_names[@]} -gt 0 ]]; then
            haisos_summary="$haisos_summary | FAILED: ${haisos_failed_names[*]}"
        fi
        echo "$haisos_summary"
    else
        echo "-/- haisos tests pass"
    fi
    echo "========================================"
}

run_windows_via_wsl() {
    local args=("$PLATFORM_ARG" "$SELECTOR_ARG")
    if [[ "$CONFIGS" == "debug" ]]; then
        args+=("--debug")
    elif [[ "$CONFIGS" == "both" ]]; then
        args+=("--both")
    fi
    if [[ -n "$FILTER" ]]; then
        args+=("$FILTER")
    fi

    echo ""
    echo "Invoking Windows tests via WSL..."
    cd "$SCRIPT_DIR"
    if cmd.exe /c test_windows.bat "${args[@]}"; then
        :
    else
        ANY_FAILED=1
    fi
}

for platform in $PLATFORMS; do
    if [[ "$platform" == "W" ]]; then
        if [[ "$IS_WSL" -eq 1 ]]; then
            run_windows_via_wsl
        else
            echo ""
            echo "Windows tests skipped (not running on WSL)"
        fi
        continue
    fi

    for config in "${CONFIG_LIST[@]}"; do
        run_tests_for_platform "$platform" "$config"
    done
done

if [[ "$ANY_FAILED" -eq 1 ]]; then
    exit 1
fi
exit 0
