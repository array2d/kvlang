#!/usr/bin/env python3
"""Compare full interpreters on the repository's unchanged benchmark cases."""
import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('benchmark', ROOT / 'benchmark/run.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, env, cpu):
    start = time.perf_counter_ns()
    result = subprocess.run(['taskset', '-c', str(cpu), *command], env=env,
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f'{command}: {result.stderr}')
    return result, time.perf_counter_ns() - start


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline', required=True, type=Path)
    ap.add_argument('--candidate', required=True, type=Path)
    ap.add_argument('--baseline-backend', required=True, type=Path)
    ap.add_argument('--candidate-backend', required=True, type=Path)
    ap.add_argument('--allocator-backend', type=Path)
    ap.add_argument('--all-backend', type=Path)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--pairs', type=int, default=9)
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--cases', default=','.join(benchmark.SWEEP))
    args = ap.parse_args()
    if args.pairs < 1:
        ap.error('--pairs must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    variants = {
        'main': (args.baseline, args.baseline_backend),
        'runtime': (args.candidate, args.baseline_backend),
        'backend': (args.baseline, args.candidate_backend),
        'combined': (args.candidate, args.candidate_backend),
    }
    if args.allocator_backend:
        variants['allocator'] = (args.baseline, args.allocator_backend)
    if args.all_backend:
        variants['all'] = (args.candidate, args.all_backend)
    metadata = {'cpu': args.cpu, 'pairs': args.pairs, 'python': platform.python_version(),
                'cpu_model': benchmark.cpu_model(), 'variants': {}, 'sources': {}}
    for name, (binary, backend) in variants.items():
        with tempfile.TemporaryDirectory(prefix='kvlang-library-check-') as td:
            env = dict(os.environ, KVSPACE='shm://' + td + '/store',
                       KVSPACE_BACKEND_PATH=str(backend), LD_DEBUG='libs', LOG_LEVEL='warn')
            result, _ = run([str(binary), '-c', 'println("ready")'], env, args.cpu)
        paths = re.findall(r'calling init:\s*(/\S+)', result.stderr)
        metadata['variants'][name] = {
            'binary': str(binary), 'backend_path': str(backend), 'binary_sha256': sha(binary),
            'libraries': {p: sha(p) for p in paths if 'libkv' in Path(p).name},
        }
    rows = []
    with (args.output / 'samples.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['case', 'scale', 'pair', 'variant',
                                              'kernel_ns', 'wall_ns', 'output_sha256'],
                                lineterminator='\n')
        writer.writeheader()
        for case in args.cases.split(','):
            scale = benchmark.SWEEP[case][-1]
            directory = ROOT / 'benchmark/cases' / case
            kv_source = directory / (case + '.kv')
            py_source = directory / (case + '.py')
            metadata['sources'][case] = {'scale': scale, 'kv_sha256': sha(kv_source),
                                         'python_sha256': sha(py_source)}
            for pair in range(args.pairs):
                order = [*variants, 'python']
                if pair % 2:
                    order.reverse()
                expected = None
                for name in order:
                    with tempfile.TemporaryDirectory(prefix='kvlang-paired-') as td:
                        env = dict(os.environ, BENCH_SCALE=str(scale), LOG_LEVEL='warn')
                        if name == 'python':
                            command = ['python3', str(py_source)]
                        else:
                            binary, backend = variants[name]
                            source = Path(td) / (case + '.kv')
                            source.write_text(kv_source.read_text().replace('__SCALE__', str(scale)))
                            env.update(KVSPACE='shm://' + td + '/store',
                                       KVSPACE_BACKEND_PATH=str(backend))
                            command = [str(binary), str(source)]
                        result, wall = run(command, env, args.cpu)
                    ns, _, output = benchmark.parse(result.stdout)
                    if ns is None or ns <= 0:
                        raise RuntimeError(f'missing time: {name}/{case}: {result.stdout}')
                    if expected is not None and output != expected:
                        raise RuntimeError(f'output mismatch: {name}/{case}: {output!r} != {expected!r}')
                    expected = output
                    row = dict(case=case, scale=scale, pair=pair, variant=name,
                               kernel_ns=ns, wall_ns=wall,
                               output_sha256=hashlib.sha256(output.encode()).hexdigest())
                    rows.append(row)
                    writer.writerow(row)
                    f.flush()
                print(f'{case} pair {pair + 1}/{args.pairs}: valid', flush=True)
    summary = {}
    for case in args.cases.split(','):
        medians = {name: statistics.median(r['kernel_ns'] for r in rows
                    if r['case'] == case and r['variant'] == name) / 1e6
                   for name in [*variants, 'python']}
        summary[case] = medians
        print(case, json.dumps(medians), flush=True)
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'medians-ms.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
