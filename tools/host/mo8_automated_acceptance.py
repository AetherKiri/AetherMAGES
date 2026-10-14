#!/usr/bin/env python3
"""Original-resolution MO8 regression checks; no OCR or all-frame claim."""
import argparse
import hashlib
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image

ROIS = {
    'body': (510, 825, 1730, 950),
    'name': (70, 950, 400, 1035),
    'ribbon': (0, 940, 640, 1044),
    'portrait': (55, 600, 450, 940),
    'scene': (0, 0, 1920, 590),
    'controls': (1780, 790, 1860, 1070),
}

# _system.scx InstSetMesModeFormat: mode 1 @0x1a34 sets WindowPos
# (123,70), MaxLineWidth 1034 at 720p, scaled 1.5 by the engine.
# Its full-screen narration is above the ADV box; use its own text area.
TEXT_ROIS = {0: ROIS['body'], 1: (184, 105, 1736, 800)}


def crop(a, box):
    x, y, u, v = box
    return a[y:v, x:u]


def stats(a):
    white = (a.min(2) > 200) & (a.max(2) - a.min(2) < 40)
    return {'white_pixels': int(white.sum()), 'std': float(a.std()),
            'black_fraction': float((a.sum(2) < 30).mean())}


def compare(a, b):
    d = np.abs(a - b)
    return {'mae_255': float(d.mean()),
            'exact_fraction': float((d.max(2) == 0).mean()),
            'within_10_fraction': float((d.max(2) <= 10).mean())}


def load(path):
    with Image.open(path) as im:
        im.load()
        if im.size != (1920, 1080) or im.mode != 'RGB':
            raise ValueError(f'Unexpected layout: {path}: {im.size}/{im.mode}')
        target = path.with_suffix('.png')
        if target.exists():
            with Image.open(target) as png:
                if png.size != im.size or png.mode != im.mode or png.tobytes() != im.tobytes():
                    raise ValueError(f'PNG does not preserve source RGB: {target}')
        else:
            im.save(target, compress_level=1)
        return np.asarray(im).astype(np.int16)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('long', type=Path)
    p.add_argument('exact', type=Path)
    p.add_argument('--baseline', type=Path, default=Path('/tmp/opencode/round8/final-current'))
    p.add_argument('--targets', type=Path, default=Path('/tmp/opencode/round8/stable_targets.txt'))
    args = p.parse_args()
    log = (args.long / 'run.log').read_text(errors='replace')
    exact_log = (args.exact / 'run.log').read_text(errors='replace')
    report = {'method': 'native 1920x1080 RGB, no resampling, no registration',
              'limits': ['Checks apply to saved captures and logged execution only.',
                         'Pixel equality to accepted PC baseline verifies four exact glyph sequences; no OCR.',
                         'Other stable/choice images receive structural checks, not translation verification.',
                          'Movie decode/render/timing checks are in movie-acceptance.json.'],
              'rois': ROIS, 'failures': [], 'exact': [], 'captures': []}
    failures = report['failures']
    target_rows = [line.split('\t', 1) for line in args.targets.read_text().splitlines()]
    for label, signature in target_rows:
        path = args.exact / f'stable_{label}.ppm'
        if not path.exists():
            failures.append(f'Missing exact target {label}')
            continue
        a, b = load(path), load(args.baseline / path.name)
        item = {'label': label, 'path': str(path), 'glyph_signature': signature,
                 'signature_found': signature in exact_log,
                 'settled_capture_logged': f'reference {label} settled' in exact_log,
                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                 'whole': compare(a, b),
                 'regions': {key: compare(crop(a, box), crop(b, box)) for key, box in ROIS.items()}}
        if not item['signature_found'] or not item['settled_capture_logged']:
            failures.append(f'Missing glyph/settle evidence {label}')
        for key, result in item['regions'].items():
            if result['mae_255'] > 1 or result['within_10_fraction'] < 0.99:
                failures.append(f'Exact target {label} differs in {key}')
        report['exact'].append(item)
    last_text, last_render, last_portrait = None, None, None
    contexts = {}
    for line in log.splitlines():
        if '[texttrace] page 0 AddString:' in line:
            last_text = line
        if '[texttrace] page 0 render:' in line:
            last_render = line
        if '[portraittrace] draw page=0 ' in line:
            last_portrait = line
        if '[longrun] capture ' in line:
            m = re.search(r'path=(.*?) ok=(\d)', line)
            if m:
                contexts[Path(m[1]).name] = {'text': last_text, 'render': last_render,
                                           'portrait': last_portrait, 'capture': line}
                if m[2] != '1':
                    failures.append(line)
    for path in sorted(args.long.glob('*.ppm')):
        try:
            a = load(path)
        except Exception as e:
            failures.append(str(e))
            continue
        kind = path.stem.rsplit('_', 1)[0]
        item = {'path': str(path), 'kind': kind, 'size': [1920, 1080],
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'global': stats(a), 'regions': {k: stats(crop(a, b)) for k, b in ROIS.items()},
                'context': contexts.get(path.name)}
        if not item['context']:
            failures.append(f'No capture evidence {path.name}')
        if kind == 'stable':
            render = (item['context'] or {}).get('render') or ''
            if 'state=3 ' not in render:
                failures.append(f'Stable capture lacks Shown evidence {path.name}')
            mode_match = re.search(r'mode=(\d+)', render)
            mode = int(mode_match[1]) if mode_match else None
            text_roi = TEXT_ROIS.get(mode)
            item['text_mode'] = mode
            item['text_roi'] = text_roi
            item['text_structure'] = stats(crop(a, text_roi)) if text_roi else None
            if text_roi is None:
                failures.append(f'No text ROI defined for mode {mode}: {path.name}')
            elif item['text_structure']['white_pixels'] < 50 or item['global']['black_fraction'] > 0.95:
                failures.append(f'Stable capture has empty/black body {path.name}')
            text = (item['context'] or {}).get('text') or ''
            named = re.search(r'name\[(\d+)\]', text)
            if named and int(named[1]) > 0:
                name = crop(a, (110, 966, 270, 1016))
                ribbon = crop(a, ROIS['ribbon'])
                blue = (ribbon[:, :, 2] > 150) & (ribbon[:, :, 2] > ribbon[:, :, 0] + 40) & (ribbon[:, :, 0] > 60)
                item['name_ink'] = stats(name)
                item['ribbon_blue_pixels'] = int(blue.sum())
                if item['name_ink']['white_pixels'] < 50 or item['ribbon_blue_pixels'] < 1000:
                    failures.append(f'Named stable capture lacks ink/ribbon {path.name}')
        if kind == 'choice':
            rows = []
            for y in (210, 303, 396):
                row = stats(crop(a, (550, y + 8, 1390, y + 74)))
                rows.append(row)
                if row['white_pixels'] < 100 or row['std'] < 10:
                    failures.append(f'Choice row has insufficient structure {path.name} y={y}')
            item['choice_rows'] = rows
        report['captures'].append(item)
    choices = []
    current_choice = None
    for line in log.splitlines():
        if '[choicetrace] show ' in line:
            current_choice = {'show': line, 'selected_ids': [],
                              'new_dialogue_after_confirmation': False}
            choices.append(current_choice)
        if current_choice is not None:
            selected = re.search(r'\[choicetrace\] selected=(\d+)', line)
            if selected:
                current_choice['selected_ids'].append(int(selected[1]))
            if current_choice['selected_ids'] and '[texttrace] page 0 AddString:' in line:
                current_choice['new_dialogue_after_confirmation'] = True
    report['choice_outcomes'] = choices
    if len(choices) != 5:
        failures.append('Expected five choice menus on the accepted route')
    for index, choice in enumerate(choices):
        if not choice['selected_ids'] or set(choice['selected_ids']) != {0} or not choice['new_dialogue_after_confirmation']:
            failures.append(f'Choice {index + 1} did not confirm first option and continue dialogue')
    exit_info = json.loads((args.long / 'exit.json').read_text())
    report['exit'] = exit_info
    report['finished'] = re.findall(r'\[longrun\] finished .*', log)
    if exit_info['exit_code'] != 0 or not re.search(r'finished frame=240000 requested=240000 .*status=0', log) or '[longrun] done' not in log:
        failures.append('240000 tick run did not finish successfully')
    report['blockers'] = [s for s in log.splitlines() if 'possible-blocker' in s or 'settle-timeout' in s]
    report['movie_limitations'] = [s for s in log.splitlines() if 'Unsupported codec' in s or 'playback cancelled' in s]
    failures.extend(report['movie_limitations'])
    report['runtime_errors'] = [s for s in log.splitlines() if re.search(r'FATAL:|tick-error|readback-error|Thread CRASH|unrecognized token|unsupported FACE|Failed to decode|Error encountered while seeking', s)]
    failures.extend(report['runtime_errors'])
    failures.extend(report['blockers'])
    report['status'] = 'PASS' if not failures else 'FAIL'
    out = args.long / 'automated-acceptance.json'
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('status', 'exit', 'finished', 'failures', 'movie_limitations')}, ensure_ascii=False, indent=2))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
