"""V5 re-scored from the SAVED raw recordings (no re-run), 2026-09-25.

The original pass rule ("the control has no hitch in the window") was falsified by the world itself: at
this view the GPU frame alternates ~4.2 / ~9.5 ms every other frame (drawFrame/Fence Wait dominant), and
the alternation phase-shifts, so the same-parity baseline flags some of it. That is a REAL finding
(logged in PerfProgram section 16.8), not a fault in the tool. V5's question is narrower: does the tool
catch a hitch WE caused and name its cause? Criterion:
  * the fill run has a hitch in the window whose top phase is Frame/API Drain or drawFrame/Dirty Chunk Flush;
  * the control has NO hitch with either of those causes, and none larger than 50 ms.
Usage: v5_rescore.py out.json"""
import json, sys

from hitch_analysis import hitches

WINDOW = (7.8, 10.5)
CAUSES = ('API Drain', 'Dirty Chunk Flush')
ctrl = json.load(open('v5_raw_control.json'))
fill = json.load(open('v5_raw_fill.json'))


def caused(h):
    return bool(h['top_phases']) and any(h['top_phases'][0][0].endswith(c) for c in CAUSES)


hc, hf = hitches(ctrl, WINDOW), hitches(fill, WINDOW)
out = {
    'window_s': WINDOW,
    'fill_caused_hitches': [h for h in hf if caused(h)],
    'control_caused_hitches': [h for h in hc if caused(h)],
    'control_max_hitch_ms': max((h['frame_ms'] for h in hc), default=0.0),
    'control_hitches_in_window': len(hc),
    'fill_hitches_in_window': len(hf),
    'original_prediction_control_hitch_free': 'FALSIFIED (GPU every-other-frame alternation at this view)',
}
out['pass'] = bool(out['fill_caused_hitches']) and not out['control_caused_hitches'] and out['control_max_hitch_ms'] < 50.0
print(json.dumps(out, indent=1))
json.dump(out, open(sys.argv[1], 'w'), indent=1)
print('V5', 'PASS' if out['pass'] else 'FAIL')
