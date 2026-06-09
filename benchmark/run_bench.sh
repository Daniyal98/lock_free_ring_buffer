#!/usr/bin/env bash
set -euo pipefail

BINARY="${1:-./ring_buffer_bench}"
OUTPUT="${2:-results.json}"

if [[ ! -x "$BINARY" ]]; then
    echo "Error: benchmark binary not found at '$BINARY'" >&2
    echo "Build with: cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build ." >&2
    exit 1
fi

OS="$(uname -s)"

if [[ "$OS" == "Linux" ]]; then
    # Disable turbo boost on Intel (requires root; skip silently if unavailable)
    if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
        echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo > /dev/null && \
            echo "Turbo boost disabled." || echo "Warning: could not disable turbo boost."
    fi

    echo "Running benchmarks pinned to cores 2,3 -> $OUTPUT"
    taskset -c 2,3 "$BINARY" \
        --benchmark_time_unit=ns \
        --benchmark_repetitions=10 \
        --benchmark_report_aggregates_only=true \
        --benchmark_format=json > "$OUTPUT"

elif [[ "$OS" == "Darwin" ]]; then
    # macOS: no taskset; caffeinate prevents CPU throttling/sleep during the run
    echo "Note: CPU pinning (taskset) is not supported on macOS. Running with caffeinate."
    echo "For best results, close other apps and disable Spotlight/Time Machine."
    echo "Running benchmarks -> $OUTPUT"
    caffeinate -i "$BINARY" \
        --benchmark_time_unit=ns \
        --benchmark_repetitions=10 \
        --benchmark_report_aggregates_only=true \
        --benchmark_format=json > "$OUTPUT"

else
    echo "Warning: unknown OS '$OS', running without CPU pinning."
    "$BINARY" \
        --benchmark_repetitions=5 \
        --benchmark_report_aggregates_only=true \
        --benchmark_format=json > "$OUTPUT"
fi

echo "Done. Results written to $OUTPUT"
