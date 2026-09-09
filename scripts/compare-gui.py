#!/usr/bin/env python3
"""Compare actual compositor versions in RAM; timings are microseconds/operation."""
import argparse
import os
from pathlib import Path
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='HEAD', help='git revision before the change')
    parser.add_argument('--runs', type=int, default=7)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    repo = Path(__file__).resolve().parents[1]
    compiler = os.environ.get('CC', 'cc')
    print(subprocess.check_output([compiler, '--version'], text=True).splitlines()[0])
    revision = subprocess.check_output(['git', 'rev-parse', args.baseline], cwd=repo, text=True).strip()
    print(f'baseline={revision}, runs={args.runs}; RAM only, 1024x768, 640x400 window')
    with tempfile.TemporaryDirectory(prefix='plant-gui-bench-') as temp:
        root = Path(temp)
        baseline = subprocess.check_output(
            ['git', 'show', f'{args.baseline}:apps/gui/sheet.c'], cwd=repo, text=True)
        # Only substitute the old umbrella include with the extracted, unchanged
        # compositor declarations. Both executables use the same test harness.
        (root / 'before.c').write_text(baseline.replace('#include "gui.h"', '#include "sheet.h"'))
        for name, source in [('before', root / 'before.c'), ('after', repo / 'apps/gui/sheet.c')]:
            subprocess.run([compiler, '-O2', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(repo / 'apps/gui'), str(source),
                            str(repo / 'scripts/tests/gui-compositor.c'), '-o', str(root / name)], check=True)
        for covered in (0, 32):
            results = {'before': {}, 'after': {}}
            # Alternate order; independent pixel validation runs every time.
            for run in range(args.runs):
                for name in (('before', 'after') if run % 2 == 0 else ('after', 'before')):
                    output = subprocess.check_output([str(root / name), str(covered)], text=True)
                    for line in output.splitlines():
                        case, value = line.split()
                        results[name].setdefault(case, []).append(float(value))
            print(f'covered layers={covered}')
            print('case           before us/op   after us/op   speedup (median)')
            for case, samples in results['before'].items():
                before = statistics.median(samples)
                after = statistics.median(results['after'][case])
                speedup = f'{before / after:.2f}x' if after >= .01 else 'no-op'
                print(f'{case:14s} {before:12.3f} {after:13.3f} {speedup:>9s}')
        print('PASS: pixel ownership, RGB/BGR, padded pitch, transparency, clipping, stacking, hide/free')


if __name__ == '__main__':
    main()
