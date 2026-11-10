#!/usr/bin/env bash
# Usage: benchmark/run_bench.sh [build_dir] [cpu_a] [cpu_b]
# cpu_a and cpu_b must be different *physical* cores: check `lscpu -e` (SMT siblings
# share a CORE number and would make the numbers look unrealistically good).
set -euo pipefail

BUILD="${1:-build}"
CPU_A="${2:-2}"
CPU_B="${3:-4}"

for bin in ring_buffer_bench ring_buffer_latency; do
    if [[ ! -x "$BUILD/$bin" ]]; then
        echo "Missing $BUILD/$bin. Build first:" >&2
        echo "  cmake -B $BUILD -DCMAKE_BUILD_TYPE=Release && cmake --build $BUILD -j" >&2
        exit 1
    fi
done

if [[ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
    echo "CPU governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)" \
         "(use 'performance' for stable numbers)"
fi

LFRB_CPU_PRODUCER="$CPU_A" LFRB_CPU_CONSUMER="$CPU_B" "$BUILD/ring_buffer_bench" \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out="$BUILD/throughput.json" --benchmark_out_format=json

echo
"$BUILD/ring_buffer_latency" "$CPU_A" "$CPU_B"
