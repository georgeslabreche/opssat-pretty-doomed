"""
Derive a meaningful output subdirectory name from an input file or directory path.

Extracts distinctive path components (pack-*, run-*, capture-*) to create
a deterministic subfolder name. Re-running the same input overwrites rather
than creating duplicates.

Examples:
  /data/capture-artifacts/pack-4023_1772797685/chg/toGround/run-000001/capture-001/capture.sc16
    -> pack-4023_1772797685_run-000001_capture-001

  /data/capture-artifacts/pack-4023_1772797685/chg/toGround/run-000001
    -> pack-4023_1772797685_run-000001

  /data/loopback-artifacts/run-000001
    -> run-000001
"""
import os


def derive_output_subdir(input_path):
    """Extract distinctive components from a path to use as output subfolder name."""
    parts = input_path.replace("\\", "/").rstrip("/").split("/")
    interesting = []
    for p in parts:
        # Match pack-NNNN_*, run-NNNNNN, capture-NNN (not capture-artifacts)
        if p.startswith("pack-") or (p.startswith("run-") and p[4:].isdigit()) \
                or (p.startswith("capture-") and p[8:].isdigit()):
            interesting.append(p)
    if interesting:
        return "_".join(interesting)
    # Fallback: use the last meaningful directory name or filename (without extension)
    basename = os.path.basename(input_path)
    name, _ = os.path.splitext(basename)
    return name or "output"


def resolve_output_dir(base_output_dir, input_path):
    """Create and return the full output directory path with a derived subfolder."""
    subdir = derive_output_subdir(input_path)
    full_path = os.path.join(base_output_dir, subdir)
    os.makedirs(full_path, exist_ok=True)
    return full_path
