"""Summarise a P1 scene: per pose x component, the paired B-A delta (component REMOVED minus baseline)
with a bootstrap 95% CI, per scope. Negative = removing it saves that much (its cost).
Usage: python summarize.py <scene-tag> [--md out.md]"""
import json, os, statistics, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', 'tools'))
from perf_harness import bootstrap_median_ci, scope_median, MIN_PAIRS_FOR_VERDICT  # noqa: E402

D = os.path.dirname(os.path.abspath(__file__))
TAG = sys.argv[1]
COMPONENTS = ['lights_off', 'main_no_micro', 'main_no_sub', 'shadow_no_micro', 'shadow_no_sub', 'foliage_off',
              # second pass: the tier's cost with shading removed (baseline = lights off / raster-only mode 11)
              'nolights_main_no_micro', 'nolights_main_no_sub', 'raster_main_no_micro', 'raster_main_no_sub']
SCOPES = ['GPU Frame', 'Scene Pass/Static Geometry', 'Shadow Pass', 'Scene Pass/Grass', 'Scene Pass/Foliage']
SHORT = {'GPU Frame': 'frame', 'Scene Pass/Static Geometry': 'static', 'Shadow Pass': 'shadow',
         'Scene Pass/Grass': 'grass', 'Scene Pass/Foliage': 'foliage'}


def cell(diffs):
    if len(diffs) < MIN_PAIRS_FOR_VERDICT:
        return f'n={len(diffs)} (no verdict)'
    lo, hi = bootstrap_median_ci(diffs)
    sig = '*' if (lo > 0 or hi < 0) else ' '
    return f'{statistics.median(diffs):+7.2f} [{lo:+.2f},{hi:+.2f}]{sig}'


lines = []
base_rows = {}
for comp in COMPONENTS:
    path = os.path.join(D, f'{TAG}_{comp}.jsonl')
    if not os.path.exists(path):
        continue
    rows = [json.loads(l) for l in open(path)]
    body = [r for r in rows if 'pose' in r and 'refused' not in r]
    for pose in dict.fromkeys(r['pose'] for r in body):
        by_rep = {}
        for r in body:
            if r['pose'] == pose:
                by_rep.setdefault(r['rep'], {})[r['config']] = r
        pairs = [(d['A'], d['B']) for d in by_rep.values() if 'A' in d and 'B' in d]
        base_rows.setdefault(pose, []).extend(a for a, _ in pairs)
        cells = []
        for s in SCOPES:
            diffs = [scope_median(b, s) - scope_median(a, s) for a, b in pairs
                     if scope_median(a, s) is not None and scope_median(b, s) is not None]
            cells.append(cell(diffs))
        refused = sum(1 for r in rows if r.get('refused') and r.get('pose') == pose)
        lines.append((pose, comp, cells, len(pairs), refused))

print(f'# {TAG}: cost of each component = (removed - baseline), ms, paired median [95% CI], * = CI excludes 0\n')
for pose in dict.fromkeys(p for p, *_ in lines):
    base = base_rows.get(pose, [])
    meds = {s: statistics.median([scope_median(r, s) for r in base if scope_median(r, s) is not None])
            for s in SCOPES if any(scope_median(r, s) is not None for r in base)}
    print(f'## pose {pose}: baseline medians (all runs): ' + ', '.join(f'{SHORT[s]} {v:.2f}' for s, v in meds.items()))
    print('| component | pairs | ' + ' | '.join(SHORT[s] for s in SCOPES) + ' |')
    print('|---|---|' + '---|' * len(SCOPES))
    for p, comp, cells, n, refused in lines:
        if p == pose:
            print(f'| {comp} | {n}{" (+%d refused)" % refused if refused else ""} | ' + ' | '.join(cells) + ' |')
    print()
