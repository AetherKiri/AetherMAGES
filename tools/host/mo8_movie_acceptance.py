#!/usr/bin/env python3
"""Compare engine OP01 playback, independent decoding and native captures."""
import argparse
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('story', type=Path)
    parser.add_argument('decode', type=Path, help='OP01 decode log')
    parser.add_argument('rgb', type=Path, help='independent RGB capture directory')
    args = parser.parse_args()
    lines = (args.story / 'run.log').read_text().splitlines()
    decoded = dict(re.findall(r'frame n=(\d+).*yuv_hash=(\w+)', args.decode.read_text()))
    failures = []
    report = {'failures': failures, 'frames': [], 'captures': [],
              'limits': ['Selected visible YUV hashes cover the decoder integration.',
                         'RGB comparison allows shader/chroma interpolation rounding.',
                         'No comparison with the proprietary RAD decoder or all routes.']}
    pattern = r'frame file=OP01.bk2 n=(\d+) pts_us=(\d+) format=(\d+) yuv_hash=(\w+) audio_us=(\d+)'
    pending = None
    active = False
    starts = ends = 0
    for line in lines:
        if '[movietrace] start file=OP01.bk2 ' in line:
            starts += 1
            active = True
            report['start'] = line
            if 'codec=binkvideo2 ' not in line or 'loop=0' not in line:
                failures.append('Incorrect OP01 decoder/loop mode')
        match = re.search(pattern, line)
        if match:
            n, pts, fmt, value, audio = match.groups()
            row = {'n': int(n), 'pts_us': int(pts), 'format': int(fmt),
                   'yuv_hash': value, 'hash_equal': decoded.get(n) == value,
                   'audio_us': int(audio), 'av_delta_us': int(pts) - int(audio)}
            report['frames'].append(row)
            if not row['hash_equal']:
                failures.append(f'Decoder hash mismatch at frame {n}')
            if abs(row['av_delta_us']) > 100000:
                failures.append(f'Audio/video clock difference above 100ms at {n}')
            pending = int(n)
        if pending and '[longrun] capture ' in line and 'kind=movie ' in line:
            path = Path(re.search(r'path=(.*?) ok=', line)[1])
            if pending in (150, 900, 1800, 3000):
                reference = args.rgb / f'decode_{pending:06d}.ppm'
                with Image.open(path) as image, Image.open(reference) as base:
                    if image.size != (1920, 1080) or base.size != image.size:
                        failures.append(f'Invalid RGB capture size at {pending}')
                    a = np.asarray(image).astype(np.int16)
                    b = np.asarray(base).astype(np.int16)
                    diff = np.abs(a - b)
                    row = {'n': pending, 'path': str(path), 'reference': str(reference),
                           'mae_255': float(diff.mean()),
                           'within_10_fraction': float((diff.max(2) <= 10).mean()),
                           'std': float(a.std())}
                    report['captures'].append(row)
                    if row['mae_255'] > 3 or row['within_10_fraction'] < 0.95:
                        failures.append(f'RGB framebuffer differs from decode at {pending}')
                    image.save(path.with_suffix('.png'), compress_level=1)
            pending = None
        if '[movietrace] end file=OP01.bk2 ' in line:
            ends += 1
            report['end'] = line
            if 'presented=3750 ' not in line:
                failures.append('OP01 ended without all 3750 frames')
        if active and '[movietrace] audio submitted=' in line:
            report['audio'] = line
            if 'submitted=5512500 remaining=0 decoder_eof=1' not in line:
                failures.append('OP01 audio did not drain exactly 125 seconds at 44100Hz')
            active = False
    expected = [1] + list(range(150, 3751, 150))
    if [r['n'] for r in report['frames']] != expected:
        failures.append('Incomplete/out-of-order OP01 frame trace')
    if starts != 1 or ends != 1 or 'audio' not in report:
        failures.append('Missing OP01 start/natural end/audio completion')
    report['loop_ends'] = [s for s in lines if '[movietrace] end ' in s and 'loop=1' in s]
    if not any('file=EFF_BG15B.bk2 ' in s for s in report['loop_ends']):
        failures.append('No complete EFF_BG15B background loop')
    if report['frames']:
        report['maximum_av_delta_us'] = max(abs(r['av_delta_us']) for r in report['frames'])
    if len(report['captures']) != 4:
        failures.append('Missing four independent RGB comparisons')
    end_index = next((i for i, s in enumerate(lines) if '[movietrace] end file=OP01.bk2 ' in s), len(lines))
    report['dialogue_after_movie'] = sum('[texttrace] page 0 AddString:' in s for s in lines[end_index + 1:])
    if not report['dialogue_after_movie']:
        failures.append('No subsequent dialogue after OP01')
    summary = next((s for s in lines if s.startswith('[longrun] movies ')), '')
    report['audio_output_statistics'] = summary
    if not re.search(r'audio_samples=[1-9]\d* nonzero=[1-9]\d* nonfinite=0 ', summary):
        failures.append('Missing finite/nonzero movie output samples')
    report['status'] = 'FAIL' if failures else 'PASS'
    (args.story / 'movie-acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'failures': failures,
                      'captures': report['captures']}, indent=2))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
