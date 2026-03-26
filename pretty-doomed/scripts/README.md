# Scripts

Data collection, visualization, and analysis scripts for pretty-doomed experiment runs.

```
scripts/
├── plots/                  # Log and resource visualization
├── report/                 # Ground-side signal analysis
└── misc/                   # SEPP runtime utilities
    └── resource-monitor.sh
```

## plots/

Timeline and resource utilization plots from log files and resource CSVs. See [plots/README.md](plots/README.md).

## report/

Ground-side post-downlink analysis. Generates HTML reports with I/Q signal analysis, spectrograms, PSD plots, and statistics from SDR capture artifacts. See [report/README.md](report/README.md).

## misc/

### resource-monitor.sh

Logs per-core CPU ticks and memory stats to a CSV file every second. Runs on the SEPP during experiment execution.

**Output**: `<run_dir>/resource.csv` with cumulative `/proc/stat` CPU ticks and `/proc/meminfo` snapshots. Diff consecutive rows to get per-second utilization.

```bash
# Standalone (run for 5 minutes)
./scripts/misc/resource-monitor.sh /path/to/output 300

# Sourceable from run script
. ./scripts/misc/resource-monitor.sh
MON_PID=$(start_monitor "$RUN_DIR" 3600)
# ... run experiment ...
stop_monitor "$MON_PID"
```
