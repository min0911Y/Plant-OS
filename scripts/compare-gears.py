#!/usr/bin/env python3
"""Compare glxgears cold-start logs, preserving samples and aggregate timings."""
import argparse
import json
from pathlib import Path
import re


def collect(directories):
    runs = []
    config = None
    for directory in directories:
        text = (directory / 'serial.log').read_text()
        match = re.search(r'GLXGEARS CONFIG cpus=(\d+) workers=(\S+) size=(\d+x\d+)', text)
        if not match or 'GLXGEARS PASS' not in text:
            raise ValueError(f'{directory}: incomplete benchmark')
        current = {
            'guest': dict(zip(('cpus', 'workers', 'size'), match.groups())),
            'qemu': json.loads((directory / 'configuration.json').read_text()),
        }
        if config is not None and current != config:
            raise ValueError('QEMU / guest / worker / window configurations differ')
        config = current
        samples = [dict(zip(('round', 'frames', 'elapsed_ns', 'render_ns', 'present_ns'),
                            map(int, values))) for values in re.findall(
            r'GLXGEARS BENCH round=(\d+) frames=(\d+) elapsed_ns=(\d+) render_ns=(\d+) present_ns=(\d+)', text)]
        if [sample['round'] for sample in samples] != [0, 1, 2]:
            raise ValueError(f'{directory}: expected three complete rounds')
        runs.append({'directory': str(directory), 'samples': samples})
    totals = {key: sum(sample[key] for run in runs for sample in run['samples'])
              for key in ('frames', 'elapsed_ns', 'render_ns', 'present_ns')}
    summary = {'fps': totals['frames'] * 1e9 / totals['elapsed_ns']}
    summary.update({key.replace('_ns', '_us_per_frame'): totals[key] / totals['frames'] / 1000
                    for key in ('elapsed_ns', 'render_ns', 'present_ns')})
    return {'configuration': config, 'runs': runs, 'summary': summary}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, nargs='+', required=True)
    parser.add_argument('--after', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = {'before': collect(args.before), 'after': collect(args.after)}
    if result['before']['configuration'] != result['after']['configuration']:
        raise ValueError('before / after guest configurations differ')
    print('QEMU and guest configurations match; host load / affinity must be controlled by the caller.')
    print('metric                  before         after        change')
    for key, before in result['before']['summary'].items():
        after = result['after']['summary'][key]
        print(f'{key:23s} {before:12.3f} {after:13.3f} {(after / before - 1) * 100:+9.2f}%')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
