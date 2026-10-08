#!/usr/bin/env python3
"""Paired full-suite runtime comparison with the frozen repository workloads."""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('benchmark', ROOT / 'benchmark/run.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'baseline', 'candidate', 'frontend', 'backend', 'output'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--pairs', type=int, default=9)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--scheme', choices=('shm', 'fs'), default='shm')
    ap.add_argument('--baseline-backend', type=Path)
    ap.add_argument('--builds', type=Path, nargs='+', default=[])
    args = ap.parse_args()
    if args.pairs < 1:
        ap.error('--pairs must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    variants = {'baseline': args.baseline, 'candidate': args.candidate, 'python': None}
    metadata = dict(cpu=args.cpu, cpu_model=benchmark.cpu_model(),
                    python=platform.python_version(), scheme=args.scheme,
                    pairs=args.pairs, command=sys.argv, files={}, loaded={}, sources={}, builds={})
    for path in (args.binary, args.baseline, args.candidate, args.frontend):
        metadata['files'][str(path)] = sha(path)
    for build in args.builds:
        fields = dict(re.findall(r'^(CMAKE_\w+):[^=]+=(.*)$',
                                 (build / 'CMakeCache.txt').read_text(), re.MULTILINE))
        source = fields['CMAKE_HOME_DIRECTORY']
        info = {k: v for k, v in fields.items()
                if k in ('CMAKE_BUILD_TYPE', 'CMAKE_C_COMPILER', 'CMAKE_HOME_DIRECTORY')
                or k.startswith('CMAKE_C_FLAGS')}
        info['head'] = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'],
                                               text=True).strip()
        diff = subprocess.check_output(['git', '-C', source, 'diff', 'HEAD'])
        info['diff_sha256'] = hashlib.sha256(diff).hexdigest()
        info['compiler'] = subprocess.check_output([fields['CMAKE_C_COMPILER'], '--version'],
                                                   text=True).splitlines()[0]
        metadata['builds'][str(build)] = info
    rows = []

    def run(case, scale, variant, probe=False):
        with tempfile.TemporaryDirectory(prefix='kvlang-overall-') as td:
            env = dict(os.environ, BENCH_SCALE=str(scale), LOG_LEVEL='warn')
            directory = ROOT / 'benchmark/cases' / case
            if variant == 'python':
                command = ['python3', str(directory / (case + '.py'))]
                env.pop('LD_PRELOAD', None)
            else:
                source = Path(td) / (case + '.kv')
                source.write_text((directory / (case + '.kv')).read_text()
                                  .replace('__SCALE__', str(scale)))
                env.update(KVSPACE=f'{args.scheme}://{td}/store',
                           KVSPACE_BACKEND_PATH=str(args.baseline_backend
                               if variant == 'baseline' and args.baseline_backend
                               else args.backend),
                           LD_PRELOAD=f'{variants[variant]}:{args.frontend}')
                if probe:
                    env['LD_DEBUG'] = 'libs'
                command = [str(args.binary), '-c', 'println("ready")'] if probe else [
                    str(args.binary), str(source)]
            start = time.perf_counter_ns()
            result = subprocess.run(['taskset', '-c', str(args.cpu), *command],
                                    cwd=ROOT, env=env, capture_output=True,
                                    text=True, timeout=180)
            wall = time.perf_counter_ns() - start
            if result.returncode:
                raise RuntimeError(f'{variant}/{case}/{scale}: {result.stderr}')
            return result, wall

    for variant in ('baseline', 'candidate'):
        result, _ = run('iops', 500, variant, probe=True)
        paths = re.findall(r'calling init:\s*(/\S+)', result.stderr)
        metadata['loaded'][variant] = {
            p: sha(p) for p in paths if 'libkv' in Path(p).name}
        if str(variants[variant]) not in metadata['loaded'][variant]:
            raise RuntimeError(f'expected runtime was not loaded: {variant}')
    with (args.output / 'samples.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['case', 'scale', 'pair', 'variant',
                                              'kernel_ns', 'wall_ns', 'output_sha256'],
                                lineterminator='\n')
        writer.writeheader()
        for case, scales in benchmark.SWEEP.items():
            directory = ROOT / 'benchmark/cases' / case
            metadata['sources'][case] = {
                ext: sha(directory / (case + '.' + ext)) for ext in ('kv', 'py')}
            for scale in scales:
                expected = None
                for pair in range(args.pairs):
                    order = list(variants)[::1 if pair % 2 == 0 else -1]
                    for variant in order:
                        result, wall = run(case, scale, variant)
                        ns, _, output = benchmark.parse(result.stdout)
                        if ns is None or ns <= 0:
                            raise RuntimeError(f'missing timer: {variant}/{case}/{scale}')
                        if expected is not None and output != expected:
                            raise RuntimeError(f'output mismatch: {variant}/{case}/{scale}')
                        expected = output
                        row = dict(case=case, scale=scale, pair=pair, variant=variant,
                                   kernel_ns=ns, wall_ns=wall,
                                   output_sha256=hashlib.sha256(output.encode()).hexdigest())
                        rows.append(row)
                        writer.writerow(row)
                        f.flush()
                print(f'{case}/{scale}: {args.pairs} valid pairs', flush=True)
    points = []
    for case, scales in benchmark.SWEEP.items():
        for scale in scales:
            med = {variant: statistics.median(r['kernel_ns'] for r in rows
                   if (r['case'], r['scale'], r['variant']) == (case, scale, variant))
                   for variant in variants}
            points.append(dict(case=case, scale=scale, medians_ns=med,
                               wall_medians_ns={variant: statistics.median(r['wall_ns'] for r in rows
                                   if (r['case'], r['scale'], r['variant']) == (case, scale, variant))
                                   for variant in variants},
                               speedup=med['baseline'] / med['candidate'],
                               python_ratio=med['candidate'] / med['python']))
    gains = [p['speedup'] for p in points]
    summary = dict(points=points, geometric_mean=math.exp(statistics.mean(map(math.log, gains))),
                   wall_geometric_mean=math.exp(statistics.mean(math.log(
                       p['wall_medians_ns']['baseline'] / p['wall_medians_ns']['candidate'])
                       for p in points)),
                   minimum=min(gains), maximum=max(gains), samples=len(rows))
    for files in (metadata['files'], *metadata['loaded'].values()):
        if any(sha(path) != digest for path, digest in files.items()):
            raise RuntimeError('a measured binary changed during the run')
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'points'}), flush=True)


if __name__ == '__main__':
    main()
