#!/usr/bin/env python3
"""Print phase medians from fsbench_v1 CSV files; reject smoke/failed runs."""
import csv
import statistics
import sys
from pathlib import Path

if len(sys.argv)<2:raise SystemExit('usage: compare.py RESULTS.csv [RESULTS.csv ...]')
print('file,platform,mode,phase,rounds,median_ms,median_MiB_per_s,median_operations_per_s')
for name in sys.argv[1:]:
    text=Path(name).read_text()
    if '# PASS verified all file contents;' not in text:raise SystemExit(f'{name}: incomplete or failed benchmark')
    rows=list(csv.DictReader(line for line in text.splitlines() if line and not line.startswith('#')))
    groups={}
    for row in rows:
        if row['fsbench_v1']!='fsbench_v1' or row['kind']!='benchmark':raise SystemExit(f'{name}: not a benchmark result')
        groups.setdefault((row['platform'],row['mode'],row['phase']),[]).append(row)
    if len(groups)!=5:raise SystemExit(f'{name}: missing benchmark phases')
    for (platform,mode,phase),values in groups.items():
        if len({v['round'] for v in values})!=len(values):raise SystemExit(f'{name}: duplicate rounds')
        median=lambda field:statistics.median(float(v[field]) for v in values)
        print(f'{Path(name).name},{platform},{mode},{phase},{len(values)},{median("elapsed_us")/1000:.3f},{median("MiB_per_s"):.3f},{median("operations_per_s"):.3f}')
