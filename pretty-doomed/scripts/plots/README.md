# Timeline and Resource Plots

Gantt-style phase timelines and combined CPU/memory utilization plots from experiment log files and resource CSVs.

## Setup

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

## Scripts

| Script | Purpose |
|--------|---------|
| `plot_log_timeline.py` | Gantt-style phase timeline from pretty-doomed log files |
| `plot_resource.py` | Combined thread timeline + per-core CPU + memory |

## Usage

All examples assume you are running from the `pretty-doomed/` root directory.

### Batch mode (default input: `toGround/`)

```bash
python3 scripts/plots/plot_log_timeline.py --batch
python3 scripts/plots/plot_resource.py --batch
```

Output is auto-numbered under `artifacts/plots/local/`.

### Custom input/output directories

```bash
python3 scripts/plots/plot_log_timeline.py --batch \
  --input-dir docs/changelog/data/em-v3/pack-4023_1774447112 \
  --output-dir docs/changelog/data/em-v3/pack-4023_1774447112

python3 scripts/plots/plot_resource.py --batch \
  --input-dir docs/changelog/data/em-v3/pack-4023_1774447112 \
  --output-dir docs/changelog/data/em-v3/pack-4023_1774447112
```

### Single run

```bash
python3 scripts/plots/plot_log_timeline.py toGround/run-00001/pretty-doomed.log output.png "Run 1"
python3 scripts/plots/plot_resource.py toGround/run-00001 --output-dir /tmp/plots
```
