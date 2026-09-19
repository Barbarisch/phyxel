#!/usr/bin/env python3
"""Keep docs/LightingPipeline.md true to the shaders (docs/LightingPipeline.md section 0.4).

    python tools/lighting_doc_check.py --check    # run by build_and_test.ps1; non-zero = stale/violation
    python tools/lighting_doc_check.py --update   # stamp the current shared-include fingerprint into the doc

Three checks, each a defect that shipped:
  1. FINGERPRINT - the SHA-256 of shaders/lighting.glsl + shaders/occupancy.glsl (the shared lighting
     model) must match the one stamped in the doc. Change the model, update the doc, re-stamp.
  2. MATRIX - every shader that includes lighting.glsl or occupancy.glsl must be named in the doc's
     receiver matrix (section 0.2), so a new receiver cannot appear undocumented.
  3. RULE R1 - no direct-sun line multiplies a sky gate (phxSkyGate( / skyGate / skyCurve) together
     with a shadowFactor unless it goes through phxSunGate(...). That multiply was the "low poly
     shadows" defect (Ravenmere G-135).
  4. RULE R8 - no receiver (.frag / .vert) calls phxSkyVisibility or phxSkyGate. The per-fragment
     5-ray sky trace is a bounce estimate inside gi_probe.comp only; as a receiver term it made an
     exterior wall black (Ravenmere G-141).
  5. RULE R9 - no receiver calls phxAmbientAtmos with anything but full sky (1.0): the probe field
     (gi_field.glsl phxAmbient) is the one ambient owner; the analytic hemisphere is its fallback.
"""
import hashlib, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, 'docs', 'LightingPipeline.md')
SHADERS = os.path.join(ROOT, 'shaders')
SHARED = ['lighting.glsl', 'occupancy.glsl', 'gi_field.glsl']
MARK = '<!-- lighting-model-fingerprint:'
# Vertex stages that include the model only to trace sky for the fragment stage; the fragment
# shader is the receiver named in the matrix.
MATRIX_EXEMPT = {'grass.vert', 'grass_shadow.vert', 'post_process.frag', 'sky.frag'}


def fingerprint():
    h = hashlib.sha256()
    for name in SHARED:
        with open(os.path.join(SHADERS, name), 'rb') as f:
            h.update(f.read())
    return h.hexdigest()[:16]


def read_doc():
    with open(DOC, encoding='utf-8') as f:
        return f.read()


def shaders_including_model():
    out = []
    for name in sorted(os.listdir(SHADERS)):
        if not (name.endswith('.frag') or name.endswith('.vert') or name.endswith('.comp')):
            continue
        with open(os.path.join(SHADERS, name), encoding='utf-8', errors='replace') as f:
            src = f.read()
        if any(f'#include "{s}"' in src for s in SHARED):
            out.append(name)
    return out


def rule_r1_violations():
    bad = []
    for name in sorted(os.listdir(SHADERS)):
        if not name.endswith('.frag'):
            continue
        with open(os.path.join(SHADERS, name), encoding='utf-8', errors='replace') as f:
            for ln, line in enumerate(f, 1):
                code = line.split('//')[0]
                if 'shadowFactor' not in code and 'shadowF' not in code:
                    continue
                if not re.search(r'phxSkyGate\(|\bskyGate\b|\bskyCurve\b', code):
                    continue
                if 'phxSunGate(' in code:
                    continue
                if 'min(' in code or '=' not in code:
                    continue
                bad.append(f'{name}:{ln}: {line.strip()}')
    return bad


def rule_r8_r9_violations():
    """R8: receivers never trace their own sky. R9: receivers never call the analytic ambient with a
    sky scalar. gi_probe.comp (the probe pass) and gi_field.glsl (the owner) are the only exceptions."""
    bad = []
    for name in sorted(os.listdir(SHADERS)):
        if not (name.endswith('.frag') or name.endswith('.vert')):
            continue
        with open(os.path.join(SHADERS, name), encoding='utf-8', errors='replace') as f:
            for ln, line in enumerate(f, 1):
                code = line.split('//')[0]
                if re.search(r'phxSkyVisibility\s*\(|phxSkyGate\s*\(', code):
                    bad.append(f'R8 {name}:{ln}: {line.strip()}')
                m = re.search(r'phxAmbientAtmos\s*\(([^;]*)\)', code)
                if m:
                    args = [a.strip() for a in m.group(1).split(',')]
                    if len(args) < 2 or args[1] != '1.0':
                        bad.append(f'R9 {name}:{ln}: {line.strip()}')
    return bad


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else '--check'
    doc = read_doc()
    fp = fingerprint()
    if mode == '--update':
        new = re.sub(re.escape(MARK) + r'[^\n]*-->', f'{MARK} {fp} -->', doc)
        if new == doc and fp not in doc:
            print('lighting_doc_check: fingerprint marker missing from the doc', file=sys.stderr)
            return 2
        with open(DOC, 'w', encoding='utf-8', newline='') as f:
            f.write(new)
        print(f'lighting_doc_check: stamped {fp}')
        return 0

    errors = []
    m = re.search(re.escape(MARK) + r'\s*([0-9a-f]{16})', doc)
    if not m:
        errors.append('doc has no fingerprint stamp - run tools/lighting_doc_check.py --update after updating section 0')
    elif m.group(1) != fp:
        errors.append(f'lighting.glsl/occupancy.glsl/gi_field.glsl changed (fingerprint {fp}, doc has {m.group(1)}): '
                      'update docs/LightingPipeline.md section 0 + section 9, then --update')
    for name in shaders_including_model():
        if name in MATRIX_EXEMPT:
            continue
        if f'`{name}`' not in doc and name not in doc:
            errors.append(f'{name} includes the shared lighting model but is not in the receiver matrix (section 0.2)')
    for v in rule_r1_violations():
        errors.append(f'R1: a direct-sun term multiplies a sky gate without phxSunGate -> {v}')
    for v in rule_r8_r9_violations():
        errors.append(f'{v[:2]}: receivers do not trace sky / do not gate the analytic ambient (G-141) -> {v[3:]}')
    if errors:
        print('lighting_doc_check FAILED:')
        for e in errors:
            print('  - ' + e)
        return 1
    print(f'lighting_doc_check: ok (model {fp}, {len(shaders_including_model())} shaders on the shared model)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
