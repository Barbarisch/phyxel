"""Compare a rung's build-time fingerprint against its reload fingerprint (PerfProgram 2026-09 section
16.1 persistence gate). Prints every differing field; exit code 0 = identical, 1 = differs.
Usage: fingerprint_diff.py <prefix>        e.g. fingerprint_diff.py city_C25M"""
import json, sys


def flat(d, p=''):
    out = {}
    for k, v in d.items():
        if isinstance(v, dict):
            out.update(flat(v, p + k + '.'))
        else:
            out[p + k] = v
    return out


prefix = sys.argv[1]
a = flat(json.load(open(prefix + '_fingerprint.json'))['fingerprint'])
b = flat(json.load(open(prefix + '_fingerprint_reload.json'))['fingerprint'])
diff = [(k, a.get(k), b.get(k)) for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]
print(f'{prefix}: {len(a)} fields, {len(diff)} differ')
for k, x, y in diff:
    print(f'  {k}: build={x} reload={y}')
sys.exit(1 if diff else 0)
