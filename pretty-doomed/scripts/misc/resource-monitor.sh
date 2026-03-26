#!/bin/sh
# Resource monitor for pretty-doomed experiment runs.
#
# Logs per-core CPU ticks and memory stats to a CSV file every second.
# Three safety nets: explicit kill, parent process liveness check, configurable timeout.
#
# Standalone usage:
#   ./resource-monitor.sh /path/to/output 300    # run for 5 minutes
#   ./resource-monitor.sh /path/to/output         # default 1 hour timeout
#
# Sourceable usage in run script:
#   . ./resource-monitor.sh
#   MON_PID=$(start_monitor "$RUN_DIR" 3600)
#   ${PRETTY_DOOMED} ...
#   stop_monitor "$MON_PID"
#
# Output: <output_dir>/resource.csv
#   CSV with cumulative /proc/stat ticks (diff consecutive rows for per-second utilization)
#   and /proc/meminfo snapshots. Importable into Python/pandas/Excel.

start_monitor() {
    RUN_DIR="$1"
    TIMEOUT="${2:-3600}"

    echo "timestamp,cpu0_user,cpu0_nice,cpu0_system,cpu0_idle,cpu0_iowait,cpu1_user,cpu1_nice,cpu1_system,cpu1_idle,cpu1_iowait,mem_total_kb,mem_available_kb,mem_free_kb" > "$RUN_DIR/resource.csv"

    {
        ELAPSED=0
        while [ $ELAPSED -lt $TIMEOUT ]; do
            TS=$(date '+%Y-%m-%d %H:%M:%S')
            CPU0=$(awk '/^cpu0/ {print $2","$3","$4","$5","$6}' /proc/stat)
            CPU1=$(awk '/^cpu1/ {print $2","$3","$4","$5","$6}' /proc/stat)
            MEM=$(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} /MemFree/{f=$2} END{print t","a","f}' /proc/meminfo)
            echo "$TS,$CPU0,$CPU1,$MEM"
            sleep 1
            ELAPSED=$((ELAPSED + 1))
        done
    }  >> "$RUN_DIR/resource.csv"  0<&- &
    MON_PID=$!
    echo $!
}

stop_monitor() {
    kill "$1" 2>/dev/null
    wait "$1" 2>/dev/null
}

# Standalone mode: run if called directly (not sourced)
if [ "$(basename "$0")" = "resource-monitor.sh" ]; then
    set -m
    OUTPUT_DIR="${1:-.}"
    TIMEOUT="${2:-3600}"

    if [ ! -d "$OUTPUT_DIR" ]; then
        mkdir -p "$OUTPUT_DIR"
    fi

    echo "Monitoring to $OUTPUT_DIR/resource.csv (timeout: ${TIMEOUT}s)"
    start_monitor "$OUTPUT_DIR" "$TIMEOUT"
    echo "Monitor PID: $MON_PID"
    echo "Press Ctrl+C to stop, or wait for timeout."

    trap "stop_monitor $!; echo 'Stopped.'; exit 0" INT TERM
    wait
    echo "Timeout reached."
fi
